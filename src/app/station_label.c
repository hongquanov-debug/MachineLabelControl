/*
 * TÓM TẮT (tiếng Việt): trạm dán tem 6 đầu hút.
 *  Mỗi lon tới -> run_cycle(): mâm M3 quay 60 độ, sau đó trong thời gian dừng:
 *   - đầu ở 0 độ  : bật hút, thổi hỗ trợ P7, M4 kéo tem tới khe (cảm biến SR21)
 *   - đầu ở 60 độ : đọc công tắc chân không -> hút được tem chưa
 *   - đầu ở 180 độ: hạ P5, ép tem, ngắt hút + thổi nhả, nâng P5
 *  Vòng lặp dùng vTaskDelayUntil(1 ms) và so sánh thời gian t (ms từ đầu chu kỳ)
 *  với các mốc của Spec 3.2 (160 ms hạ P5, 210 ms ép, ...).
 */
#include "station_label.h"

#include <math.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_board.h"
#include "io.h"
#include "motion.h"
#include "recipe.h"
#include "task.h"
#include "tracking.h"

typedef enum { HEAD_EMPTY = 0, HEAD_LOADED, HEAD_OK, HEAD_MISS } HeadState_t;

#define M3_PULSES_PER_REV  100000L   /* 10000 ppr x 10 (hollow rotary table) */
#define LABEL_FEED_TIMEOUT_MS 300U

static HeadState_t s_head[LABEL_HEADS];
static uint8_t s_index;              /* 0..5 : number of 60 deg steps since home (mod 6) */
static volatile bool s_prime_req;

static const DoBit_t k_vac_do[LABEL_HEADS] = {DO_VAC_HEAD_1, DO_VAC_HEAD_2, DO_VAC_HEAD_3,
                                              DO_VAC_HEAD_4, DO_VAC_HEAD_5, DO_VAC_HEAD_6};
static const DiBit_t k_vac_di[LABEL_HEADS] = {DI_VAC_SW_1, DI_VAC_SW_2, DI_VAC_SW_3,
                                              DI_VAC_SW_4, DI_VAC_SW_5, DI_VAC_SW_6};

/** Head currently at station k (k = 0 -> 0 deg, 1 -> 60 deg ... 5 -> 300 deg). */
static inline uint8_t head_at(uint8_t k)
{
    return (uint8_t)((k + LABEL_HEADS - s_index) % LABEL_HEADS);
}

void station_label_request_prime(void) { s_prime_req = true; }

void station_label_reset_heads(void)
{
    for (uint8_t h = 0; h < LABEL_HEADS; h++) {
        s_head[h] = HEAD_EMPTY;
    }
    s_index = 0;
}

void station_label_safe(void)
{
    do_set(DO_CYL_P5, false);
    do_set(DO_VALVE_P7, false);
    do_set(DO_VAC_BLOW, false);
    for (uint8_t h = 0; h < LABEL_HEADS; h++) {
        do_set(k_vac_do[h], false);
    }
    do_commit_now();
}

/* Absolute index target keeps 100000/6 pulses exact (no drift over revolutions) */
static bool m3_index_start(void)
{
    int32_t pos = bsp_pulse_position(AXIS_M3);
    if (s_index == 0U && pos >= M3_PULSES_PER_REV - 10) {
        pos -= M3_PULSES_PER_REV;
        bsp_pulse_set_position(AXIS_M3, pos);
    }
    int32_t target = (int32_t)lroundf((float)(s_index + 1U) * (float)M3_PULSES_PER_REV / (float)LABEL_HEADS);
    int32_t steps  = target - pos;
    PulseProfile_t prof;
    ramp_profile_from_times((uint32_t)labs(steps), M3_INDEX_ACC_MS / 1000.0f, M3_INDEX_CONST_MS / 1000.0f,
                            PULSE_F_START_HZ, &prof);
    if (prof.f_max > M3_VMAX_HZ) {
        prof.f_max = M3_VMAX_HZ;
    }
    return motion_move_pulses(AXIS_M3, steps, &prof);
}

/* M4: pull label strip until the gap sensor, then overrun N pulses (Spec §4 / 40006) */
static bool m4_feed_start(void)
{
    return motion_jog(AXIS_M4, true, 1.0f);
}

typedef struct {
    bool label_can;          /* this cycle has a can to label */
    uint16_t can_id;
    uint16_t t_release, t_up;
    uint16_t vac_detect;
    uint16_t overrun;
} CycleCfg_t;

static void edit_status(CanRecord_t *r, void *ctx) { r->status = *(CanStatus_t *)ctx; }

static void mark_can(uint16_t id, CanStatus_t st)
{
    tracking_edit(id, edit_status, &st);
}

/**
 * Run one index + dwell cycle. Returns false on a fatal station fault.
 */
static bool run_cycle(const CycleCfg_t *cfg)
{
    TickType_t t0 = xTaskGetTickCount();
    TickType_t wake = t0;

    if (!m3_index_start()) {
        return false;
    }
    if (!motion_wait(AXIS_M3, M3_INDEX_TOTAL_MS + 50U)) {
        alarm_raise(ALM_11_SERVO_M3_FAULT);
        return false;
    }
    s_index = (uint8_t)((s_index + 1U) % LABEL_HEADS);
    uint32_t t_dwell = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;

    const uint8_t h_feed  = head_at(0);
    const uint8_t h_check = head_at(1);
    const uint8_t h_label = head_at(3);
    const uint8_t h_clean = head_at(4);

    bool label_ok = cfg->label_can && (s_head[h_label] == HEAD_OK);
    if (cfg->label_can && !label_ok) {
        mark_can(cfg->can_id, CAN_ERROR_VACUUM_MISS);   /* ALM_06: skip this can */
    }

    /* dwell start actions */
    do_set(k_vac_do[h_feed], true);
    do_set(k_vac_do[h_clean], false);
    do_commit_now();
    bool m4_running = m4_feed_start();
    bool m4_gap_seen = false;
    bool p5_down_cmd = false, p5_down_ok = false, released = false, p5_up_cmd = false, p5_up_ok = !label_ok;
    bool checked = false, p7_on = false, p7_done = false;
    TickType_t t_p5_cmd = 0;

    for (;;) {
        uint32_t t = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;  /* ms since cycle start */
        uint32_t td = (t > t_dwell) ? t - t_dwell : 0U;                  /* ms since dwell start */

        /* ---- feed station (0 deg) ---- */
        if (!p7_on && !p7_done && td >= LABEL_FEED_BLOW_DELAY_MS) {
            do_set(DO_VALVE_P7, true);
            p7_on = true;
        } else if (p7_on && td >= LABEL_FEED_BLOW_DELAY_MS + LABEL_FEED_BLOW_MS) {
            do_set(DO_VALVE_P7, false);
            p7_on = false;
            p7_done = true;
            s_head[h_feed] = HEAD_LOADED;
        }
        if (m4_running && !m4_gap_seen && bsp_home_active(HOME_M4)) {
            bsp_pulse_finish_after(AXIS_M4, cfg->overrun);
            m4_gap_seen = true;
        }
        if (m4_running && !motion_busy(AXIS_M4)) {
            m4_running = false;
        }
        if (m4_running && !m4_gap_seen && td >= LABEL_FEED_TIMEOUT_MS) {
            motion_stop(AXIS_M4);
            alarm_raise(ALM_07_LABEL_TAPE_EMPTY);  /* no gap found: tape broken / empty */
            m4_running = false;
        }

        /* ---- vacuum check station (60 deg) ---- */
        if (!checked && td >= cfg->vac_detect) {
            checked = true;
            if (s_head[h_check] != HEAD_EMPTY) {
                bool ok = di(k_vac_di[h_check]);
                s_head[h_check] = ok ? HEAD_OK : HEAD_MISS;
                if (!ok) {
                    alarm_raise(ALM_06_VACUUM_FEED_MISS);   /* warning */
                }
            }
        }

        /* ---- labeling station (180 deg) ---- */
        if (label_ok) {
            if (!p5_down_cmd && t >= LABEL_T_P5_DOWN_MS) {
                do_set(DO_CYL_P5, true);
                do_commit_now();
                p5_down_cmd = true;
                t_p5_cmd = xTaskGetTickCount();
            }
            if (p5_down_cmd && !p5_down_ok) {
                if (di(DI_P5_DOWN)) {
                    p5_down_ok = true;
                } else if ((xTaskGetTickCount() - t_p5_cmd) > pdMS_TO_TICKS(CYL_P5_TIMEOUT_MS)) {
                    alarm_raise(ALM_08_CYL_P5_TIMEOUT);
                    station_label_safe();
                    motion_abort_all();
                    mark_can(cfg->can_id, CAN_ERROR_LABEL_MISS);
                    return false;
                }
            }
            if (!released && t >= cfg->t_release) {
                do_set(k_vac_do[h_label], false);
                do_set(DO_VAC_BLOW, true);
                do_commit_now();
                released = true;
            }
            if (!p5_up_cmd && t >= cfg->t_up) {
                do_set(DO_CYL_P5, false);
                do_set(DO_VAC_BLOW, false);
                do_commit_now();
                p5_up_cmd = true;
                t_p5_cmd = xTaskGetTickCount();
                s_head[h_label] = HEAD_EMPTY;
                mark_can(cfg->can_id, p5_down_ok ? CAN_STATUS_LABELED : CAN_ERROR_LABEL_MISS);
            }
            if (p5_up_cmd && !p5_up_ok) {
                if (di(DI_P5_UP)) {
                    p5_up_ok = true;
                } else if ((xTaskGetTickCount() - t_p5_cmd) > pdMS_TO_TICKS(CYL_P5_TIMEOUT_MS) ||
                           t > LABEL_T_P5_UP_DEADLINE_MS) {
                    alarm_raise(ALM_08_CYL_P5_TIMEOUT);   /* stop M3 to protect the heads */
                    station_label_safe();
                    motion_abort_all();
                    return false;
                }
            }
        } else if (s_head[h_label] == HEAD_MISS) {
            do_set(k_vac_do[h_label], false);           /* drop the bad head */
            s_head[h_label] = HEAD_EMPTY;
        }

        bool done = p7_done && checked && !m4_running && p5_up_ok && (!label_ok || p5_up_cmd);
        if (done) {
            break;
        }
        if (t > LABEL_CYCLE_MS + 100U) {
            station_label_safe();
            return false;
        }
        vTaskDelayUntil(&wake, 1);
    }
    return true;
}

void station_label_task(void *arg)
{
    (void)arg;
    StationEvent_t ev;
    for (;;) {
        if (s_prime_req) {
            /* load heads 0..3 so that the 180 deg head carries a label */
            s_prime_req = false;
            CanRecipe_t rcp;
            recipe_get_active(&rcp);
            CycleCfg_t cfg = {false, 0, 0, 0, rcp.vacuum_detect_ms, rcp.m4_overrun_pulses};
            for (int i = 0; i < 4 && s_head[head_at(2)] != HEAD_OK; i++) {
                if (!run_cycle(&cfg)) {
                    break;
                }
            }
        }

        if (xQueueReceive(tracking_q_label(), &ev, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if ((xTaskGetTickCount() - ev.tick) > pdMS_TO_TICKS(LABEL_MAX_LATENESS_MS)) {
            mark_can(ev.can_id, CAN_ERROR_LABEL_MISS);    /* station was still busy */
            continue;
        }
        CanRecipe_t rcp;
        recipe_get_active(&rcp);
        CycleCfg_t cfg;
        cfg.label_can  = true;
        cfg.can_id     = ev.can_id;
        cfg.t_up       = (uint16_t)(LABEL_T_PRESS_MS + rcp.p5_down_dwell_ms);
        cfg.t_release  = (uint16_t)(cfg.t_up - ((rcp.blow_off_time_ms < rcp.p5_down_dwell_ms) ? rcp.blow_off_time_ms
                                                                                              : rcp.p5_down_dwell_ms));
        cfg.vac_detect = rcp.vacuum_detect_ms;
        cfg.overrun    = rcp.m4_overrun_pulses;
        if (!run_cycle(&cfg)) {
            mark_can(ev.can_id, CAN_ERROR_LABEL_MISS);
        }
    }
}
