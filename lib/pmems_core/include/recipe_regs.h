/**
 * @file recipe_regs.h
 * @brief Recipe editor register block (Modbus holding registers 40021..40041).
 *
 * Used by the PC configuration tool over Ethernet (Modbus TCP, Spec §7.3) and
 * also reachable from the HMI (RTU) because both share one register map.
 *
 *  40021        edit recipe id (1..16)
 *  40022..40029 can_name, 16 ASCII chars, 2 per register (high byte first)
 *  40030        can_diameter_x10        (0.1 mm)
 *  40031        can_height_x10          (0.1 mm)
 *  40032        tab_to_label_offset     (int16, deg x100)
 *  40033        m6_height_pos_x10       (0.1 mm, absolute M6 position)
 *  40034        m2_angle_scale_x1000    READ-ONLY: computed from diameter (D / D_PU)
 *  40035        belt_speed_cans_min
 *  40036        m4_overrun_pulses
 *  40037        p5_down_dwell_ms
 *  40038        vacuum_detect_ms
 *  40039        blow_off_time_ms
 *  40040        command (write): 1 LOAD, 2 SAVE, 3 SAVE_ACTIVATE, 4 LOAD_DEFAULTS
 *  40041        status of last command (read-only), see RcpStatus_t
 *
 * Ascending order lets the PC write 40021..40040 in ONE FC16 request:
 * the buffer is filled first and the command (last register) executes after.
 */
#ifndef RECIPE_REGS_H
#define RECIPE_REGS_H

#include <stdint.h>

#include "recipe_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0-based holding-register indexes (40001 -> 0) */
#define RCP_REG_FIRST      20U   /* 40021 */
#define RCP_REG_ID         20U
#define RCP_REG_NAME       21U   /* 8 registers */
#define RCP_REG_DIAMETER   29U
#define RCP_REG_HEIGHT     30U
#define RCP_REG_TAB_OFFSET 31U
#define RCP_REG_M6_POS     32U
#define RCP_REG_M2_SCALE   33U   /* read-only */
#define RCP_REG_SPEED      34U
#define RCP_REG_OVERRUN    35U
#define RCP_REG_P5_DWELL   36U
#define RCP_REG_VAC_DETECT 37U
#define RCP_REG_BLOW_OFF   38U
#define RCP_REG_CMD        39U   /* 40040 */
#define RCP_REG_STATUS     40U   /* 40041 */
#define RCP_REG_DATA_COUNT 19U   /* 40021..40039 */

typedef enum {
    RCP_CMD_LOAD          = 1,   /* EEPROM -> edit buffer (id = 40021) */
    RCP_CMD_SAVE          = 2,   /* edit buffer -> EEPROM */
    RCP_CMD_SAVE_ACTIVATE = 3,   /* save, then make it the active recipe */
    RCP_CMD_LOAD_DEFAULTS = 4,   /* factory defaults -> edit buffer */
} RcpCmd_t;

typedef enum {
    RCP_ST_IDLE       = 0,
    RCP_ST_OK         = 1,
    RCP_ST_ERR_RANGE  = 2,   /* a value is out of range */
    RCP_ST_ERR_EEPROM = 3,   /* EEPROM write/read failed (RAM copy still updated) */
    RCP_ST_ERR_STATE  = 4,   /* active recipe cannot change in this machine state */
    RCP_ST_ERR_ID     = 5,   /* recipe id not 1..16 */
    RCP_ST_ERR_CMD    = 6,   /* unknown command */
} RcpStatus_t;

/** Diameter of the PU orientation wheel (Spec §4: Ø95) in 0.1 mm. */
#define PU_WHEEL_DIAMETER_X10 950U

/** m2_angle_scale_x1000 = round(D_can / D_PU * 1000). */
uint16_t recipe_scale_from_diameter(uint16_t can_diameter_x10, uint16_t pu_diameter_x10);

/** Recipe -> registers 40021..40039 (RCP_REG_DATA_COUNT values). */
void recipe_to_regs(const CanRecipe_t *r, uint16_t regs[RCP_REG_DATA_COUNT]);

/**
 * Registers -> recipe. The M2 scale register is ignored and recomputed from
 * the diameter. The name is always NUL-terminated. CRC is sealed.
 */
void recipe_from_regs(const uint16_t regs[RCP_REG_DATA_COUNT], uint16_t pu_diameter_x10, CanRecipe_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_REGS_H */
