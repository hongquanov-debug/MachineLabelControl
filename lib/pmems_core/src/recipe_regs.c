#include "recipe_regs.h"

#include <string.h>

#define IDX(reg) ((reg) - RCP_REG_FIRST)

uint16_t recipe_scale_from_diameter(uint16_t can_diameter_x10, uint16_t pu_diameter_x10)
{
    if (pu_diameter_x10 == 0U) {
        return 0;
    }
    return (uint16_t)(((uint32_t)can_diameter_x10 * 1000U + pu_diameter_x10 / 2U) / pu_diameter_x10);
}

void recipe_to_regs(const CanRecipe_t *r, uint16_t regs[RCP_REG_DATA_COUNT])
{
    regs[IDX(RCP_REG_ID)] = r->recipe_id;
    for (unsigned i = 0; i < 8U; i++) {
        regs[IDX(RCP_REG_NAME) + i] =
            (uint16_t)(((uint16_t)(uint8_t)r->can_name[2U * i] << 8) | (uint8_t)r->can_name[2U * i + 1U]);
    }
    regs[IDX(RCP_REG_DIAMETER)]   = r->can_diameter_x10;
    regs[IDX(RCP_REG_HEIGHT)]     = r->can_height_x10;
    regs[IDX(RCP_REG_TAB_OFFSET)] = (uint16_t)r->tab_to_label_offset;
    regs[IDX(RCP_REG_M6_POS)]     = r->m6_height_pos_x10;
    regs[IDX(RCP_REG_M2_SCALE)]   = r->m2_angle_scale_x1000;
    regs[IDX(RCP_REG_SPEED)]      = r->belt_speed_cans_min;
    regs[IDX(RCP_REG_OVERRUN)]    = r->m4_overrun_pulses;
    regs[IDX(RCP_REG_P5_DWELL)]   = r->p5_down_dwell_ms;
    regs[IDX(RCP_REG_VAC_DETECT)] = r->vacuum_detect_ms;
    regs[IDX(RCP_REG_BLOW_OFF)]   = r->blow_off_time_ms;
}

void recipe_from_regs(const uint16_t regs[RCP_REG_DATA_COUNT], uint16_t pu_diameter_x10, CanRecipe_t *r)
{
    memset(r, 0, sizeof(*r));
    r->recipe_id = (uint8_t)regs[IDX(RCP_REG_ID)];
    for (unsigned i = 0; i < 8U; i++) {
        uint16_t v = regs[IDX(RCP_REG_NAME) + i];
        r->can_name[2U * i]      = (char)(v >> 8);
        r->can_name[2U * i + 1U] = (char)(v & 0xFFU);
    }
    r->can_name[sizeof(r->can_name) - 1U] = '\0';
    r->can_diameter_x10     = regs[IDX(RCP_REG_DIAMETER)];
    r->can_height_x10       = regs[IDX(RCP_REG_HEIGHT)];
    r->tab_to_label_offset  = (int16_t)regs[IDX(RCP_REG_TAB_OFFSET)];
    r->m6_height_pos_x10    = regs[IDX(RCP_REG_M6_POS)];
    r->m2_angle_scale_x1000 = recipe_scale_from_diameter(r->can_diameter_x10, pu_diameter_x10);
    r->belt_speed_cans_min  = regs[IDX(RCP_REG_SPEED)];
    r->m4_overrun_pulses    = regs[IDX(RCP_REG_OVERRUN)];
    r->p5_down_dwell_ms     = regs[IDX(RCP_REG_P5_DWELL)];
    r->vacuum_detect_ms     = regs[IDX(RCP_REG_VAC_DETECT)];
    r->blow_off_time_ms     = regs[IDX(RCP_REG_BLOW_OFF)];
    recipe_seal(r);
}
