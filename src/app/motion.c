/*
 * TÓM TẮT (tiếng Việt): lớp trục động cơ.
 *  Đổi đơn vị kỹ thuật (độ, mm) sang số xung theo bảng k_axis[] (Spec Mục 4),
 *  kiểm tra giới hạn mềm, bật/tắt driver và chạy về gốc (homing).
 *  Phần phát xung thực sự nằm ở src/bsp/bsp_pulse.c.
 */
#include "motion.h"

#include <math.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "bsp_board.h"
#include "io.h"
#include "task.h"

static const AxisParam_t k_axis[AXIS_COUNT] = {
    [AXIS_M2] = {"M2", "deg", M2_PULSES_PER_DEG, M2_VMAX_HZ, M2_ACCEL_HZ_S, 0.0f, false, false,
                 false, -180.0f, 180.0f, 0.0f},
    [AXIS_M3] = {"M3", "deg", M3_PULSES_PER_DEG, M3_VMAX_HZ, M3_ACCEL_HZ_S, M3_HOMING_HZ, false, true,
                 false, 0.0f, 360.0f, -M3_HOME_OFFSET_DEG},
    [AXIS_M4] = {"M4", "mm", M4_PULSES_PER_MM, M4_VMAX_HZ, M4_ACCEL_HZ_S, M4_HOMING_HZ, true, true,
                 false, 0.0f, 0.0f, 0.0f},
    [AXIS_M5] = {"M5", "mm", M5_PULSES_PER_MM, M5_VMAX_HZ, M5_ACCEL_HZ_S, M5_HOMING_HZ, false, true,
                 true, M5_SOFT_MIN_MM, M5_SOFT_MAX_MM, 0.0f},
    [AXIS_M6] = {"M6", "mm", M6_PULSES_PER_MM, M6_VMAX_HZ, M6_ACCEL_HZ_S, M6_HOMING_HZ, true, true,
                 true, M6_SOFT_MIN_MM, M6_SOFT_MAX_MM, M6_HOME_POS_MM},
};

static const HomeSensor_t k_home_sensor[AXIS_COUNT] = {
    [AXIS_M2] = HOME_COUNT, [AXIS_M3] = HOME_M3, [AXIS_M4] = HOME_M4,
    [AXIS_M5] = HOME_M5,    [AXIS_M6] = HOME_M6A,
};

static bool s_enabled;

const AxisParam_t *motion_param(Axis_t ax) { return &k_axis[ax]; }

void motion_init(void)
{
    bsp_pulse_init();
    s_enabled = false;
}

void motion_enable_all(bool on)
{
    /* ENA inputs: '1' = driver disabled (spec §5.2). SON_M3: '1' = servo on. */
    do_set(DO_ENA_M2, !on);
    do_set(DO_ENA_M4, !on);
    do_set(DO_ENA_M5, !on);
    do_set(DO_ENA_M6, !on);
    do_set(DO_SON_M3, on);
    do_commit_now();
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(100));      /* driver/servo ready time before releasing brake */
        bsp_m6_brake_release(true);
    } else {
        motion_abort_all();
        bsp_m6_brake_release(false);
    }
    s_enabled = on;
}

bool motion_enabled(void) { return s_enabled; }

static PulseProfile_t profile(Axis_t ax, float speed_scale)
{
    if (speed_scale <= 0.0f || speed_scale > 1.0f) {
        speed_scale = 1.0f;
    }
    PulseProfile_t p = {PULSE_F_START_HZ, k_axis[ax].vmax_hz * speed_scale, k_axis[ax].accel_hz_s};
    return p;
}

bool motion_move_pulses(Axis_t ax, int32_t pulses, const PulseProfile_t *prof)
{
    if (!s_enabled) {
        return false;
    }
    if (pulses == 0) {
        return true;
    }
    return bsp_pulse_start(ax, pulses > 0, (uint32_t)labs(pulses), prof);
}

bool motion_move_abs(Axis_t ax, float pos, float speed_scale)
{
    const AxisParam_t *p = &k_axis[ax];
    if (p->has_soft_limits && (pos < p->soft_min || pos > p->soft_max)) {
        return false;
    }
    int32_t target = (int32_t)lroundf(pos * p->ppu);
    PulseProfile_t prof = profile(ax, speed_scale);
    return motion_move_pulses(ax, target - bsp_pulse_position(ax), &prof);
}

bool motion_move_rel(Axis_t ax, float dist, float speed_scale)
{
    return motion_move_abs(ax, motion_position(ax) + dist, speed_scale);
}

bool motion_jog(Axis_t ax, bool forward, float speed_scale)
{
    if (!s_enabled) {
        return false;
    }
    PulseProfile_t prof = profile(ax, speed_scale);
    return bsp_pulse_start(ax, forward, 0, &prof);
}

void motion_stop(Axis_t ax) { bsp_pulse_stop(ax); }

void motion_abort_all(void)
{
    for (int i = 0; i < (int)AXIS_COUNT; i++) {
        bsp_pulse_abort((Axis_t)i);
    }
}

bool motion_busy(Axis_t ax) { return bsp_pulse_busy(ax); }

bool motion_wait(Axis_t ax, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    while (bsp_pulse_busy(ax)) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            bsp_pulse_abort(ax);
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

float motion_position(Axis_t ax)
{
    return (float)bsp_pulse_position(ax) / k_axis[ax].ppu;
}

void motion_set_position(Axis_t ax, float pos)
{
    bsp_pulse_set_position(ax, (int32_t)lroundf(pos * k_axis[ax].ppu));
}

static bool home_hit(Axis_t ax)
{
    if (ax == AXIS_M6) {
        /* both lead screws share one pulse train: stop on the first sensor */
        return bsp_home_active(HOME_M6A) || bsp_home_active(HOME_M6B);
    }
    return bsp_home_active(k_home_sensor[ax]);
}

static bool search(Axis_t ax, bool forward, float hz, bool want_active, uint32_t timeout_ms)
{
    PulseProfile_t prof = {PULSE_F_START_HZ < hz ? PULSE_F_START_HZ : hz, hz, k_axis[ax].accel_hz_s};
    if (!bsp_pulse_start(ax, forward, 0, &prof)) {
        return false;
    }
    TickType_t start = xTaskGetTickCount();
    while (home_hit(ax) != want_active) {
        if (!bsp_pulse_busy(ax)) {
            return false;   /* aborted (E-Stop / alarm) */
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            bsp_pulse_abort(ax);
            return false;
        }
        vTaskDelay(1);
    }
    bsp_pulse_stop(ax);
    return motion_wait(ax, 2000);
}

bool motion_home_axis(Axis_t ax, uint32_t timeout_ms)
{
    const AxisParam_t *p = &k_axis[ax];
    if (!p->has_home || !s_enabled) {
        motion_set_position(ax, 0.0f);
        return p->has_home ? false : true;
    }
    /* If already on the sensor: move off it first */
    if (home_hit(ax) && !search(ax, !p->home_forward, p->home_hz, false, timeout_ms)) {
        return false;
    }
    /* 1) fast search */
    if (!search(ax, p->home_forward, p->home_hz, true, timeout_ms)) {
        return false;
    }
    /* 2) back off until sensor released */
    if (!search(ax, !p->home_forward, p->home_hz * 0.25f, false, 5000)) {
        return false;
    }
    /* 3) slow re-approach for repeatability */
    if (!search(ax, p->home_forward, p->home_hz * 0.1f, true, 5000)) {
        return false;
    }
    if (ax == AXIS_M6 && bsp_home_active(HOME_M6A) != bsp_home_active(HOME_M6B)) {
        /* TODO(HW): tolerance check between M6a/M6b sensors (mechanical skew) */
    }
    motion_set_position(ax, p->home_pos);
    return true;
}
