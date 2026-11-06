/*
 * TÓM TẮT (tiếng Việt): trạm chỉnh góc.
 *  Nhận sự kiện từ tracking -> đẩy chốt P3 ra chặn lon -> chờ lon tới -> kẹp P4
 *  -> quay bánh PU (M2) một góc tính từ kết quả Vision -> nhả P4, rút P3.
 *  Mỗi xi lanh có cảm biến từ; không thấy tín hiệu trong thời gian cho phép
 *  -> ALM_15. Vì lon bị chặn lại, vị trí theo encoder được "đặt lại" khi nhả.
 */
#include "station_orient.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_encoder.h"
#include "io.h"
#include "motion.h"
#include "orient_math.h"
#include "params.h"
#include "recipe.h"
#include "task.h"
#include "tracking.h"

#define ARRIVAL_TIMEOUT_MS 1500U
#define SETTLE_MS          20U
#define M2_MOVE_TIMEOUT_MS 500U

static void set_status(CanRecord_t *r, void *ctx)
{
    r->status = *(CanStatus_t *)ctx;
}

static void release_hold(CanRecord_t *r, void *ctx)
{
    (void)ctx;
    /* the can was stopped by P3 at X=300 mm: restart its coordinate from there */
    r->enc_capture_s2 = bsp_encoder_now() - (uint32_t)ENC_POS_ORIENT;
    r->flags &= (uint8_t)~CAN_FLAG_HOLD;
}

void station_orient_release(void)
{
    do_set(DO_CYL_P4, false);
    do_set(DO_CYL_P3, false);
    do_commit_now();
}

static bool cyl(DoBit_t out, bool on, DiBit_t sensor)
{
    do_set(out, on);
    do_commit_now();
    if (!io_wait_di(sensor, true, CYL_GENERIC_TIMEOUT_MS)) {
        alarm_raise(ALM_15_CYLINDER_TIMEOUT);
        return false;
    }
    return true;
}

static bool wait_arrival(uint16_t can_id)
{
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        CanRecord_t r;
        if (!tracking_get(can_id, &r)) {
            return false;
        }
        if (can_distance(&r, bsp_encoder_now()) >= ENC_POS_ORIENT) {
            return true;
        }
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(ARRIVAL_TIMEOUT_MS)) {
            return false;
        }
        vTaskDelay(1);
    }
}

void station_orient_task(void *arg)
{
    (void)arg;
    StationEvent_t ev;
    for (;;) {
        if (xQueueReceive(tracking_q_orient(), &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        CanRecord_t rec;
        if (!tracking_get(ev.can_id, &rec)) {
            continue;   /* cleared (E-Stop) */
        }
        CanStatus_t result = CAN_STATUS_ORIENTED;
        bool ok = cyl(DO_CYL_P3, true, DI_P3_EXT);         /* stop pin out */
        ok = ok && wait_arrival(ev.can_id);
        if (ok) {
            vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
            ok = cyl(DO_CYL_P4, true, DI_P4_EXT);           /* clamp against PU wheel */
        }
        if (ok) {
            CanRecipe_t rcp;
            recipe_get_active(&rcp);
            int32_t pulses = orient_m2_pulses(rec.theta_degree, rcp.tab_to_label_offset,
                                              params_get()->theta_offset_x100,
                                              rcp.m2_angle_scale_x1000, M2_PULSES_PER_DEG);
            const AxisParam_t *ap = motion_param(AXIS_M2);
            PulseProfile_t prof = {PULSE_F_START_HZ, ap->vmax_hz, ap->accel_hz_s};
            ok = motion_move_pulses(AXIS_M2, pulses, &prof) && motion_wait(AXIS_M2, M2_MOVE_TIMEOUT_MS);
        }
        if (!ok) {
            result = CAN_ERROR_VISION_NG;   /* could not orient -> reject downstream */
        }
        /* always release, whatever happened */
        do_set(DO_CYL_P4, false);
        do_commit_now();
        if (!io_wait_di(DI_P4_RET, true, CYL_GENERIC_TIMEOUT_MS)) {
            alarm_raise(ALM_15_CYLINDER_TIMEOUT);
        }
        do_set(DO_CYL_P3, false);
        do_commit_now();
        if (!io_wait_di(DI_P3_RET, true, CYL_GENERIC_TIMEOUT_MS)) {
            alarm_raise(ALM_15_CYLINDER_TIMEOUT);
        }
        tracking_edit(ev.can_id, set_status, &result);
        tracking_edit(ev.can_id, release_hold, NULL);
    }
}
