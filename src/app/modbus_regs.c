#include "modbus_regs.h"

#include "alarm.h"
#include "io.h"
#include "machine.h"
#include "motion.h"
#include "params.h"
#include "recipe.h"
#include "tracking.h"
#include "vfd.h"
#include "vision.h"

static uint8_t rd_holding(void *ctx, uint16_t a, uint16_t *v)
{
    (void)ctx;
    CanRecipe_t r;
    switch (a) {
    case 0:  *v = 0; break;
    case 1:  *v = recipe_active_id(); break;
    case 2:  *v = params_get()->speed_cans_min; break;
    case 3:  *v = (uint16_t)params_get()->theta_offset_x100; break;
    case 4:  recipe_get_active(&r); *v = r.p5_down_dwell_ms; break;
    case 5:  recipe_get_active(&r); *v = r.m4_overrun_pulses; break;
    case 9:  *v = params_get()->jog_axis; break;
    case 10: *v = (uint16_t)params_get()->jog_dir; break;
    default: *v = 0; break;
    }
    return MB_EX_NONE;
}

static uint8_t wr_holding(void *ctx, uint16_t a, uint16_t v)
{
    (void)ctx;
    CanRecipe_t r;
    switch (a) {
    case 0:
        if (v < 1U || v > 7U) return MB_EX_ILLEGAL_VALUE;
        machine_post_cmd((MachineCmd_t)v);
        return MB_EX_NONE;
    case 1:
        if (v < 1U || v > 16U) return MB_EX_ILLEGAL_VALUE;
        if (!machine_recipe_change_allowed()) return MB_EX_DEVICE_FAILURE;
        if (!recipe_select((uint8_t)v)) return MB_EX_DEVICE_FAILURE;
        recipe_get_active(&r);
        params_mut()->speed_cans_min = r.belt_speed_cans_min;
        return MB_EX_NONE;
    case 2:
        if (v < 60U || v > 156U) return MB_EX_ILLEGAL_VALUE;
        params_mut()->speed_cans_min = v;
        return MB_EX_NONE;
    case 3:
        if ((int16_t)v < -1800 || (int16_t)v > 1800) return MB_EX_ILLEGAL_VALUE;
        params_mut()->theta_offset_x100 = (int16_t)v;
        return MB_EX_NONE;
    case 4:
        recipe_get_active(&r);
        r.p5_down_dwell_ms = v;
        return recipe_update_active(&r) ? MB_EX_NONE : MB_EX_ILLEGAL_VALUE;
    case 5:
        recipe_get_active(&r);
        r.m4_overrun_pulses = v;
        return recipe_update_active(&r) ? MB_EX_NONE : MB_EX_ILLEGAL_VALUE;
    case 9:
        if (v >= AXIS_COUNT) return MB_EX_ILLEGAL_VALUE;
        params_mut()->jog_axis = v;
        return MB_EX_NONE;
    case 10:
        if ((int16_t)v < -1 || (int16_t)v > 1) return MB_EX_ILLEGAL_VALUE;
        params_mut()->jog_dir = (int16_t)v;
        return MB_EX_NONE;
    default:
        return MB_EX_NONE;   /* reserved: accepted, ignored */
    }
}

static uint8_t rd_input(void *ctx, uint16_t a, uint16_t *v)
{
    (void)ctx;
    TrackingCounters_t c = tracking_counters();
    uint32_t di = di_image(), dout = do_image(), am = alarm_mask();
    VfdStatus_t vs = vfd_status();
    VisionStats_t vis = vision_stats();
    switch (a) {
    case 0:  *v = (uint16_t)machine_state_hmi(); break;
    case 1:  *v = (uint16_t)alarm_current(); break;
    case 2:  *v = tracking_speed_cans_min(); break;
    case 3:  *v = (uint16_t)(c.fed >> 16); break;
    case 4:  *v = (uint16_t)c.fed; break;
    case 5:  *v = (uint16_t)(c.ok >> 16); break;
    case 6:  *v = (uint16_t)c.ok; break;
    case 7:  *v = (uint16_t)(c.ng >> 16); break;
    case 8:  *v = (uint16_t)c.ng; break;
    case 9:  *v = (uint16_t)(motion_position(AXIS_M6) * 10.0f); break;
    case 10: *v = io_safety().air_ok ? 50U : 0U; break;  /* TODO(HW): digital switch only (>= 0.5 MPa) */
    case 11: *v = (uint16_t)(di >> 16); break;
    case 12: *v = (uint16_t)di; break;
    case 13: *v = (uint16_t)(dout >> 16); break;
    case 14: *v = (uint16_t)dout; break;
    case 15: *v = vs.out_freq_x100; break;
    case 16: *v = vs.current_x10; break;
    case 17: *v = tracking_count(); break;
    case 18: *v = (uint16_t)(am >> 16); break;
    case 19: *v = (uint16_t)am; break;
    case 20: *v = (uint16_t)vis.crc_errors; break;
    case 21: *v = (uint16_t)vis.rx_late; break;
    default: *v = 0; break;
    }
    return MB_EX_NONE;
}

static const MbMap_t k_map = {
    MB_HOLDING_COUNT, MB_INPUT_COUNT, rd_holding, wr_holding, rd_input, NULL,
};

const MbMap_t *modbus_regs_map(void) { return &k_map; }
