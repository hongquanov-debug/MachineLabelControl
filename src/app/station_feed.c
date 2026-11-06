#include "station_feed.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_encoder.h"
#include "io.h"
#include "machine.h"
#include "task.h"

#define S1_RELEASE_TIMEOUT_MS 250U

static volatile bool s_enabled;

void station_feed_enable(bool en) { s_enabled = en; }
bool station_feed_enabled(void)   { return s_enabled; }

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

void station_feed_task(void *arg)
{
    (void)arg;
    uint32_t last_release = bsp_encoder_now();

    for (;;) {
        if (!s_enabled) {
            MachineState_t ms = machine_state();
            if (ms != MS_ESTOP && ms != MS_MANUAL_JOG) {
                /* blocking position: P1 out (holds the queue), P2 back (ALM_03/ALM_07) */
                do_set(DO_CYL_P1, true);
                do_set(DO_CYL_P2, false);
            }
            last_release = bsp_encoder_now() - CAN_PITCH_PULSES;  /* release immediately on start */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t enc = bsp_encoder_now();
        if ((uint32_t)(enc - last_release) < CAN_PITCH_PULSES || !di(DI_S0_QUEUE)) {
            vTaskDelay(1);
            continue;
        }
        last_release = enc;

        /* 1. wedge P2 between can #1 and #2   2. open P1 -> can #1 leaves
         * 3. S1 confirms   4. close P1   5. retract P2 -> queue advances  */
        bool ok = cyl(DO_CYL_P2, true, DI_P2_EXT) && cyl(DO_CYL_P1, false, DI_P1_RET);
        if (ok) {
            (void)io_wait_di(DI_S1_RELEASE, true, S1_RELEASE_TIMEOUT_MS); /* missing can: harmless */
        }
        ok = cyl(DO_CYL_P1, true, DI_P1_EXT) && ok;
        ok = cyl(DO_CYL_P2, false, DI_P2_RET) && ok;
        if (!ok) {
            s_enabled = false;
        }
    }
}
