#include "modbus_regs.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "recipe_regs.h"
#include "semphr.h"
#include "io.h"
#include "machine.h"
#include "motion.h"
#include "params.h"
#include "recipe.h"
#include "tracking.h"
#include "vfd.h"
#include "vision.h"
#include "app_config.h"

/* ------------------------------------------------------------------------ */
/* Recipe editor (40021..40041). Shared by HMI (RTU) and PC (TCP) tasks.    */
/* ------------------------------------------------------------------------ */
static SemaphoreHandle_t s_rcp_mutex;
static uint16_t s_rcp_buf[RCP_REG_DATA_COUNT];
static volatile uint16_t s_rcp_status = RCP_ST_IDLE;

void modbus_regs_init(void)
{
    s_rcp_mutex = xSemaphoreCreateMutex();
    configASSERT(s_rcp_mutex != NULL);
    CanRecipe_t r;
    recipe_get_active(&r);
    recipe_to_regs(&r, s_rcp_buf);   /* buffer starts with the active recipe */
}

static uint16_t rcp_execute(uint16_t cmd)
{
    CanRecipe_t r;
    uint8_t id = (uint8_t)s_rcp_buf[RCP_REG_ID - RCP_REG_FIRST];
    if (id < 1U || id > 16U) {
        return RCP_ST_ERR_ID;
    }
    switch (cmd) {
    case RCP_CMD_LOAD:
        (void)recipe_load(id, &r);
        recipe_to_regs(&r, s_rcp_buf);
        return RCP_ST_OK;

    case RCP_CMD_LOAD_DEFAULTS:
        recipe_factory_default(id, M6_REF_POS_330ML_X10, &r);
        recipe_to_regs(&r, s_rcp_buf);
        return RCP_ST_OK;

    case RCP_CMD_SAVE:
    case RCP_CMD_SAVE_ACTIVATE: {
        bool activate = (cmd == RCP_CMD_SAVE_ACTIVATE);
        bool touches_active = activate || id == recipe_active_id();
        recipe_from_regs(s_rcp_buf, PU_WHEEL_DIAMETER_X10, &r);
        if (!recipe_check_ranges(&r)) {
            return RCP_ST_ERR_RANGE;
        }
        if (touches_active && !machine_recipe_change_allowed()) {
            return RCP_ST_ERR_STATE;   /* never change the running recipe */
        }
        bool active_changed = false;
        RecipeStoreResult_t res = recipe_store(&r, &active_changed);
        recipe_to_regs(&r, s_rcp_buf);  /* show computed M2 scale */
        if (res == RECIPE_STORE_RANGE) {
            return RCP_ST_ERR_RANGE;
        }
        if (activate && !active_changed) {
            (void)recipe_select(id);
            active_changed = true;
        }
        if (active_changed) {
            params_mut()->speed_cans_min = r.belt_speed_cans_min;
            machine_request_recipe_apply();
        }
        return (res == RECIPE_STORE_OK) ? RCP_ST_OK : RCP_ST_ERR_EEPROM;
    }
    default:
        return RCP_ST_ERR_CMD;
    }
}

static bool rcp_read(uint16_t a, uint16_t *v)
{
    if (a >= RCP_REG_FIRST && a < RCP_REG_FIRST + RCP_REG_DATA_COUNT) {
        xSemaphoreTake(s_rcp_mutex, portMAX_DELAY);
        *v = s_rcp_buf[a - RCP_REG_FIRST];
        xSemaphoreGive(s_rcp_mutex);
        return true;
    }
    if (a == RCP_REG_CMD) {
        *v = 0;
        return true;
    }
    if (a == RCP_REG_STATUS) {
        *v = s_rcp_status;
        return true;
    }
    return false;
}

static bool rcp_write(uint16_t a, uint16_t v, uint8_t *ex)
{
    *ex = MB_EX_NONE;
    if (a == RCP_REG_M2_SCALE || a == RCP_REG_STATUS) {
        return true;    /* read-only: ignored so the whole block can be written in one FC16 */
    }
    if (a >= RCP_REG_FIRST && a < RCP_REG_FIRST + RCP_REG_DATA_COUNT) {
        xSemaphoreTake(s_rcp_mutex, portMAX_DELAY);
        s_rcp_buf[a - RCP_REG_FIRST] = v;
        xSemaphoreGive(s_rcp_mutex);
        return true;
    }
    if (a == RCP_REG_CMD) {
        xSemaphoreTake(s_rcp_mutex, portMAX_DELAY);
        s_rcp_status = rcp_execute(v);   /* synchronous: done when the Modbus reply is sent */
        xSemaphoreGive(s_rcp_mutex);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------ */
static uint8_t rd_holding(void *ctx, uint16_t a, uint16_t *v)
{
    (void)ctx;
    CanRecipe_t r;
    if (rcp_read(a, v)) {
        return MB_EX_NONE;
    }
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
    uint8_t ex;
    if (rcp_write(a, v, &ex)) {
        return ex;
    }
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
        machine_request_recipe_apply();
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
    case 22: *v = (uint16_t)((machine_recipe_change_allowed() ? 1U : 0U) | (recipe_eeprom_ok() ? 2U : 0U) |
                             (machine_recipe_applying() ? 4U : 0U));
             break;
    default: *v = 0; break;
    }
    return MB_EX_NONE;
}

static const MbMap_t k_map = {
    MB_HOLDING_COUNT, MB_INPUT_COUNT, rd_holding, wr_holding, rd_input, NULL,
};

const MbMap_t *modbus_regs_map(void) { return &k_map; }
