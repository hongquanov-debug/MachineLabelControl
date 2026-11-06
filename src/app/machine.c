/**
 * @file machine.c
 * @brief Máy trạng thái của toàn hệ thống (Spec Mục 6.1), chạy mỗi 10 ms.
 *
 *  Máy trạng thái hữu hạn (FSM): tại mỗi thời điểm máy ở ĐÚNG MỘT trạng thái
 *  (s_state). fsm_step() xét lệnh từ HMI/PC + tín hiệu an toàn + alarm để
 *  quyết định có chuyển trạng thái không. Khi chuyển, enter(new_state) thực
 *  hiện các hành động "vào trạng thái" (bật băng tải, cắt KM1, ...).
 *
 *    POWER_ON -> HOMING_REQUIRED -> HOMING_EXEC -> READY <-> AUTO_RUN <-> AUTO_PAUSE
 *         \            ^                             |          |
 *          -> ESTOP ---/          ALARM_STOP <-------/----------/
 *
 *  Lệnh từ HMI/PC không gọi trực tiếp vào đây mà được gửi vào hàng đợi
 *  (machine_post_cmd) -> chỉ task machine thay đổi trạng thái, tránh xung đột.
 */
#include "machine.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_board.h"
#include "homing.h"
#include "io.h"
#include "motion.h"
#include "params.h"
#include "queue.h"
#include "recipe.h"
#include "station_feed.h"
#include "station_label.h"
#include "station_orient.h"
#include "task.h"
#include "tracking.h"
#include "vfd.h"

#define CYCLE_MS 10U

static volatile MachineState_t s_state = MS_POWER_ON;
static QueueHandle_t s_cmdq;
static volatile bool s_abort;
static volatile int8_t s_homing_result = -1;   /* -1 pending, 0 fail, 1 ok */
static bool s_need_home;
static bool s_auto_paused;                       /* pause caused by S0 empty */
static TickType_t s_state_since, s_s0_empty_since;
static uint16_t s_last_speed;
static TrackingCounters_t s_cnt_at_start;
static IWDG_HandleTypeDef s_iwdg;
static volatile bool s_apply_req;
static volatile uint16_t s_manual_req;      /* requested manual DO bits (40012) */
static volatile bool s_manual_pending;

/* ------------------------------------------------------------------------ */
void machine_init(void)
{
    s_cmdq = xQueueCreate(8, sizeof(MachineCmd_t));
    configASSERT(s_cmdq != NULL);
}

void machine_post_cmd(MachineCmd_t cmd)
{
    if (s_cmdq != NULL) {
        (void)xQueueSend(s_cmdq, &cmd, 0);
    }
}

MachineState_t machine_state(void) { return s_state; }
bool machine_abort_requested(void) { return s_abort; }
void machine_homing_done(bool ok)  { s_homing_result = ok ? 1 : 0; }
void machine_request_recipe_apply(void) { s_apply_req = true; }
bool machine_recipe_applying(void) { return s_apply_req || motion_busy(AXIS_M6); }

/* READY only: start the (non-blocking) M6 move to the recipe height */
static void apply_recipe_if_requested(void)
{
    if (!s_apply_req || s_need_home || !motion_enabled() || motion_busy(AXIS_M6)) {
        return;
    }
    CanRecipe_t r;
    recipe_get_active(&r);
    if (!motion_move_abs(AXIS_M6, (float)r.m6_height_pos_x10 / 10.0f, 1.0f)) {
        alarm_raise(ALM_16_HOMING_FAILED);   /* target outside soft limits */
    }
    s_apply_req = false;
}

bool machine_manual_outputs(uint16_t mask)
{
    if (s_state != MS_MANUAL_JOG || (mask & (uint16_t)~MANUAL_DO_MASK) != 0U) {
        return false;
    }
    /* Interlock: never lower the labeling cylinder P5 while the rotary table turns */
    if ((mask & (1U << DO_CYL_P5)) != 0U && motion_busy(AXIS_M3)) {
        return false;
    }
    s_manual_req = mask;
    s_manual_pending = true;   /* applied by the machine task (single writer) */
    return true;
}

/* All manually driven outputs off (leaving MANUAL_JOG, alarm, E-Stop) */
static void manual_outputs_off(void)
{
    for (uint8_t i = 0; i < 15U; i++) {
        do_set((DoBit_t)i, false);
    }
    s_manual_req = 0;
    s_manual_pending = false;
    do_commit_now();
}

bool machine_recipe_change_allowed(void)
{
    return s_state == MS_READY || s_state == MS_HOMING_REQUIRED || s_state == MS_ALARM_STOP ||
           s_state == MS_POWER_ON || s_state == MS_ESTOP;
}

HmiState_t machine_state_hmi(void)
{
    switch (s_state) {
    case MS_POWER_ON:        return HMI_ST_POWER_ON;
    case MS_ESTOP:           return HMI_ST_ESTOP;
    case MS_HOMING_EXEC:     return HMI_ST_HOMING;
    case MS_READY:           return HMI_ST_READY;
    case MS_AUTO_RUN:        return HMI_ST_RUNNING;
    case MS_ALARM_STOP:      return HMI_ST_ALARM;
    case MS_AUTO_PAUSE:      return HMI_ST_PAUSED;
    case MS_MANUAL_JOG:      return HMI_ST_JOG;
    case MS_HOMING_REQUIRED: return HMI_ST_HOME_REQ;
    default:                 return HMI_ST_POWER_ON;
    }
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                   */
/* ------------------------------------------------------------------------ */
static void production_stop(void)
{
    station_feed_enable(false);
    tracking_enable(false);
    vfd_stop();
    do_set(DO_MTR_M7, false);
}

static void account_lifetime(void)
{
    TrackingCounters_t c = tracking_counters();
    recipe_add_lifetime(c.fed - s_cnt_at_start.fed, c.ok - s_cnt_at_start.ok, c.ng - s_cnt_at_start.ng);
    s_cnt_at_start = c;
}

static void enter(MachineState_t st)
{
    MachineState_t prev = s_state;
    if (prev == MS_MANUAL_JOG && st != MS_MANUAL_JOG) {
        manual_outputs_off();
    }
    if (prev == MS_AUTO_RUN || prev == MS_AUTO_PAUSE) {
        if (st != MS_AUTO_RUN && st != MS_AUTO_PAUSE) {
            account_lifetime();
        }
    }

    switch (st) {
    case MS_ESTOP:
        /* ALM_01 / ALM_02: KM1 off, M6 brake, clear FIFO, all motion off */
        s_abort = true;
        bsp_km1_set(false);
        bsp_m6_brake_release(false);
        production_stop();
        motion_enable_all(false);     /* abort pulses, drivers off, brake engaged */
        io_outputs_safe();
        tracking_clear();
        s_need_home = true;
        break;

    case MS_HOMING_REQUIRED:
        bsp_km1_set(true);
        s_abort = false;
        break;

    case MS_HOMING_EXEC:
        if (homing_busy()) {
            return;   /* previous (aborted) run still unwinding: retry on next command */
        }
        s_abort = false;
        s_homing_result = -1;
        homing_start();
        break;

    case MS_READY:
        s_abort = false;
        production_stop();
        break;

    case MS_AUTO_RUN: {
        CanRecipe_t r;
        recipe_get_active(&r);
        if (prev == MS_READY) {
            s_cnt_at_start = tracking_counters();
            station_label_request_prime();
        }
        s_last_speed = params_get()->speed_cans_min;
        vfd_run(vfd_freq_for_speed(s_last_speed));
        tracking_enable(true);
        station_feed_enable(true);
        do_set(DO_MTR_M7, true);
        s_auto_paused = false;
        s_s0_empty_since = xTaskGetTickCount();
        break;
    }

    case MS_AUTO_PAUSE:
        station_feed_enable(false);   /* belt keeps running until it is empty */
        break;

    case MS_ALARM_STOP:
        s_abort = true;
        production_stop();
        motion_abort_all();
        station_label_safe();
        station_orient_release();
        if (alarm_mask() & ((1UL << ALM_10_DRIVER_M2_FAULT) | (1UL << ALM_11_SERVO_M3_FAULT) |
                            (1UL << ALM_12_DRIVER_M4_FAULT) | (1UL << ALM_13_DRIVER_M5_FAULT) |
                            (1UL << ALM_14_DRIVER_M6_FAULT) | (1UL << ALM_08_CYL_P5_TIMEOUT) |
                            (1UL << ALM_16_HOMING_FAILED))) {
            s_need_home = true;   /* axis positions no longer trusted */
        }
        break;

    case MS_MANUAL_JOG:
    case MS_POWER_ON:
    default:
        break;
    }
    s_state = st;
    s_state_since = xTaskGetTickCount();
}

static bool motion_state(MachineState_t st)
{
    return st == MS_AUTO_RUN || st == MS_AUTO_PAUSE || st == MS_HOMING_EXEC || st == MS_MANUAL_JOG;
}

static void update_lamps(void)
{
    bool blink = ((xTaskGetTickCount() / pdMS_TO_TICKS(500)) & 1U) != 0U;
    AlarmSeverity_t sev = alarm_highest_severity();
    bool red = false, yel = false, grn = false, buzz = false;

    switch (s_state) {
    case MS_ESTOP:       red = blink; buzz = true; break;
    case MS_ALARM_STOP:  red = true; buzz = blink && (xTaskGetTickCount() - s_state_since) < pdMS_TO_TICKS(10000); break;
    case MS_AUTO_RUN:    grn = true; break;
    case MS_AUTO_PAUSE:  yel = true; grn = blink; break;
    case MS_HOMING_EXEC: yel = blink; break;
    default:             break;
    }
    if (sev == ALM_SEV_WARNING || alarm_is_active(ALM_07_LABEL_TAPE_EMPTY)) {
        yel = blink;
    }
    if (alarm_is_active(ALM_04_CABINET_OVERTEMP)) {
        buzz = buzz || ((xTaskGetTickCount() / pdMS_TO_TICKS(1000)) % 4U == 0U);
    }
    do_set(DO_TOWER_RED, red);
    do_set(DO_TOWER_YEL, yel);
    do_set(DO_TOWER_GRN, grn);
    bsp_buzzer_set(buzz);
}

static int16_t cabinet_temp_c(void)
{
    return INT16_MIN;   /* TODO(HW): cabinet temperature sensor not defined in spec */
}

static void watchdog_init(void)
{
#if APP_ENABLE_WATCHDOG
    __HAL_DBGMCU_FREEZE_IWDG();
    s_iwdg.Instance       = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER_32;              /* LSI 32 kHz -> 1 kHz */
    s_iwdg.Init.Reload    = WATCHDOG_TIMEOUT_MS;
    (void)HAL_IWDG_Init(&s_iwdg);
#endif
}

static void watchdog_kick(void)
{
#if APP_ENABLE_WATCHDOG
    static uint32_t last_hb;
    uint32_t hb = io_heartbeat();
    if (hb != last_hb) {           /* I/O task (1 ms, highest priority) alive */
        last_hb = hb;
        HAL_IWDG_Refresh(&s_iwdg);
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* FSM                                                                       */
/* ------------------------------------------------------------------------ */
static void fsm_step(MachineCmd_t cmd)
{
    const SafetyStatus_t sf = io_safety();
    const AlarmSeverity_t sev = alarm_highest_severity();
    const TickType_t now = xTaskGetTickCount();

    /* --- global: E-Stop / door open while moving -> STATE_ESTOP --- */
    if (s_state != MS_ESTOP && s_state != MS_POWER_ON) {
        if (alarm_is_active(ALM_01_ESTOP_ACTIVATED) ||
            (alarm_is_active(ALM_02_SAFETY_DOOR_OPEN) && motion_state(s_state))) {
            enter(MS_ESTOP);
            return;
        }
    }
    /* door alarm outside motion states is informative only: clear once closed */
    if (!motion_state(s_state) && s_state != MS_ESTOP && sf.door_closed) {
        alarm_clear(ALM_02_SAFETY_DOOR_OPEN);
    }

    switch (s_state) {
    case MS_POWER_ON:
        if ((now - s_state_since) < pdMS_TO_TICKS(300)) {
            break;   /* let the I/O task take its first scans */
        }
        if (!sf.estop_ok || !sf.door_closed || !sf.air_ok) {
            if (!sf.estop_ok) alarm_raise(ALM_01_ESTOP_ACTIVATED);
            enter(MS_ESTOP);
        } else {
            alarm_clear_all();
            enter(MS_HOMING_REQUIRED);
        }
        break;

    case MS_ESTOP:
        if (sf.estop_ok && sf.door_closed && sf.reset_pressed) {
            alarm_clear_all();
            enter(MS_HOMING_REQUIRED);
        }
        break;

    case MS_HOMING_REQUIRED:
        if (cmd == CMD_RESET_ALARM) {
            alarm_clear_all();
        } else if (cmd == CMD_START_HOMING && sev < ALM_SEV_STOP && sf.door_closed && sf.air_ok) {
            enter(MS_HOMING_EXEC);
        }
        break;

    case MS_HOMING_EXEC:
        if (sev >= ALM_SEV_STOP) {
            s_abort = true;
            enter(MS_ALARM_STOP);
        } else if (s_homing_result == 1) {
            s_need_home = false;
            s_apply_req = false;     /* homing already moved M6 to the recipe height */
            enter(MS_READY);
        } else if (s_homing_result == 0) {
            enter(MS_ALARM_STOP);
        }
        break;

    case MS_READY:
        apply_recipe_if_requested();
        if (cmd == CMD_START && sev < ALM_SEV_DRAIN_STOP && sf.door_closed && !machine_recipe_applying()) {
            enter(MS_AUTO_RUN);
        } else if (cmd == CMD_JOG_ENTER) {
            enter(MS_MANUAL_JOG);
        } else if (cmd == CMD_START_HOMING) {
            enter(MS_HOMING_EXEC);
        } else if (cmd == CMD_RESET_ALARM) {
            alarm_clear_all();
        }
        break;

    case MS_AUTO_RUN: {
        if (sev >= ALM_SEV_STOP || cmd == CMD_STOP) {
            enter((sev >= ALM_SEV_STOP) ? MS_ALARM_STOP : MS_READY);
            break;
        }
        if (sev == ALM_SEV_DRAIN_STOP) {
            /* ALM_07: stop infeed, finish cans on the belt, then stop */
            station_feed_enable(false);
            if (tracking_count() == 0U) {
                enter(MS_ALARM_STOP);
            }
            break;
        }
        if (cmd == CMD_PAUSE) {
            s_auto_paused = false;
            enter(MS_AUTO_PAUSE);
            break;
        }
        if (di(DI_S0_QUEUE)) {
            s_s0_empty_since = now;
        } else if ((now - s_s0_empty_since) > pdMS_TO_TICKS(S0_EMPTY_PAUSE_MS)) {
            enter(MS_AUTO_PAUSE);
            s_auto_paused = true;
            break;
        }
        uint16_t spd = params_get()->speed_cans_min;
        if (spd != s_last_speed) {
            s_last_speed = spd;
            vfd_run(vfd_freq_for_speed(spd));
        }
        break;
    }

    case MS_AUTO_PAUSE:
        if (sev >= ALM_SEV_STOP) {
            enter(MS_ALARM_STOP);
        } else if (cmd == CMD_STOP) {
            enter(MS_READY);
        } else if (cmd == CMD_START || (s_auto_paused && di(DI_S0_QUEUE))) {
            enter(MS_AUTO_RUN);
        } else if (tracking_count() == 0U) {
            vfd_stop();   /* belt empty: stop conveyor while paused */
        }
        break;

    case MS_ALARM_STOP:
        if (cmd == CMD_RESET_ALARM) {
            alarm_clear_all();   /* monitors re-raise persistent conditions within 1 ms */
        } else if (alarm_mask() == 0U && (now - s_state_since) > pdMS_TO_TICKS(100) &&
                   (cmd == CMD_START_HOMING || !s_need_home)) {
            if (s_need_home) {
                enter(MS_HOMING_EXEC);
            } else {
                enter(MS_READY);
            }
        }
        break;

    case MS_MANUAL_JOG: {
        const volatile RuntimeParams_t *p = params_get();
        Axis_t ax = (Axis_t)p->jog_axis;
        if (cmd == CMD_JOG_EXIT || sev >= ALM_SEV_STOP) {
            motion_abort_all();
            params_mut()->jog_dir = 0;
            enter(sev >= ALM_SEV_STOP ? MS_ALARM_STOP : MS_READY);
        }
        /* Interlock: no M3 rotation unless P5 is confirmed up */
        bool jog_allowed = !(ax == AXIS_M3 && !di(DI_P5_UP));
        if (p->jog_dir != 0 && !motion_busy(ax) && jog_allowed) {
            (void)motion_jog(ax, p->jog_dir > 0, 0.1f);
        } else if ((p->jog_dir == 0 || !jog_allowed) && motion_busy(ax)) {
            motion_stop(ax);
        }
        /* Manual outputs requested through 40012 */
        if (s_manual_pending) {
            uint16_t m = s_manual_req;
            s_manual_pending = false;
            for (uint8_t i = 0; i < 15U; i++) {
                do_set((DoBit_t)i, ((m >> i) & 1U) != 0U);
            }
            do_commit_now();
        }
        break;
    }
    }
}

void machine_task(void *arg)
{
    (void)arg;
    watchdog_init();
    s_state_since = xTaskGetTickCount();
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        MachineCmd_t cmd = CMD_NONE;
        (void)xQueueReceive(s_cmdq, &cmd, 0);

        int16_t t = cabinet_temp_c();
        if (t != INT16_MIN && t > CABINET_OVERTEMP_C) {
            alarm_raise(ALM_04_CABINET_OVERTEMP);
        }

        fsm_step(cmd);
        update_lamps();
        io_set_drives_powered(motion_enabled() && s_state != MS_ESTOP);
        recipe_service(s_state != MS_AUTO_RUN && s_state != MS_AUTO_PAUSE && s_state != MS_HOMING_EXEC);
        watchdog_kick();

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CYCLE_MS));
    }
}
