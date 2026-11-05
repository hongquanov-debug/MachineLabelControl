#include "homing.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "io.h"
#include "machine.h"
#include "motion.h"
#include "recipe.h"
#include "station_label.h"
#include "station_orient.h"
#include "task.h"
#include "tracking.h"
#include "vfd.h"

static TaskHandle_t s_task;
static volatile bool s_busy;

void homing_start(void)
{
    if (s_task != NULL && !s_busy) {
        s_busy = true;
        xTaskNotifyGive(s_task);
    }
}

bool homing_busy(void) { return s_busy; }

static bool step_safe_positions(void)
{
    station_orient_release();
    station_label_safe();
    bool ok = io_wait_di(DI_P4_RET, true, 1000) && io_wait_di(DI_P5_UP, true, 1000);
    if (!ok) {
        alarm_raise(ALM_15_CYLINDER_TIMEOUT);
    }
    return ok;
}

static bool step_purge(void)
{
    if (machine_abort_requested()) {
        return false;
    }
    tracking_enable(false);
    vfd_run(PURGE_VFD_HZ_X100);
    vTaskDelay(pdMS_TO_TICKS(PURGE_TIME_MS));
    vfd_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    tracking_clear();
    return true;
}

static bool home(Axis_t ax)
{
    if (machine_abort_requested()) {
        return false;
    }
    return motion_home_axis(ax, HOMING_TIMEOUT_MS);
}

static bool run_sequence(void)
{
    if (!step_safe_positions() || !step_purge()) {
        return false;
    }
    if (machine_abort_requested()) {
        return false;
    }
    motion_enable_all(true);

    if (!home(AXIS_M6) || !home(AXIS_M3)) {
        return false;
    }
    /* M3: from home sensor to station 0 (+12.5 deg) */
    if (!motion_move_abs(AXIS_M3, 0.0f, 0.2f) || !motion_wait(AXIS_M3, 3000)) {
        return false;
    }
    motion_set_position(AXIS_M3, 0.0f);
    station_label_reset_heads();

    if (!home(AXIS_M4) || !home(AXIS_M5)) {
        return false;
    }
    motion_set_position(AXIS_M2, 0.0f);   /* M2: relative axis, no sensor */

    CanRecipe_t rcp;
    recipe_get_active(&rcp);
    if (!motion_move_abs(AXIS_M6, (float)rcp.m6_height_pos_x10 / 10.0f, 1.0f) ||
        !motion_wait(AXIS_M6, 10000)) {
        return false;
    }
    return true;
}

void homing_task(void *arg)
{
    (void)arg;
    s_task = xTaskGetCurrentTaskHandle();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool ok = run_sequence();
        if (!ok) {
            motion_abort_all();
            vfd_stop();
            if (!machine_abort_requested()) {
                alarm_raise(ALM_16_HOMING_FAILED);
            }
        }
        s_busy = false;
        machine_homing_done(ok);
    }
}
