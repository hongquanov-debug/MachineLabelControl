#include "recipe_types.h"

#include <stddef.h>
#include <string.h>

#include "crc16.h"

void recipe_seal(CanRecipe_t *r)
{
    r->crc16 = crc16_modbus((const uint8_t *)r, offsetof(CanRecipe_t, crc16));
}

bool recipe_valid(const CanRecipe_t *r)
{
    return r->crc16 == crc16_modbus((const uint8_t *)r, offsetof(CanRecipe_t, crc16)) &&
           r->recipe_id >= 1U && r->recipe_id <= 16U;
}

void syscfg_seal(SysConfig_t *c)
{
    c->crc16 = crc16_modbus((const uint8_t *)c, offsetof(SysConfig_t, crc16));
}

bool syscfg_valid(const SysConfig_t *c)
{
    return c->magic == SYSCFG_MAGIC && c->version == SYSCFG_VERSION &&
           c->crc16 == crc16_modbus((const uint8_t *)c, offsetof(SysConfig_t, crc16));
}

typedef struct {
    const char *name;
    uint16_t    dia_x10;
    uint16_t    h_x10;
    int16_t     m6_rel_x10;
    uint16_t    scale;
} DefaultDef_t;

static const DefaultDef_t k_defaults[4] = {
    {"CAN_330ML_STD", 660, 1152, 0, 695},     /* Recipe 1 */
    {"CAN_250ML_LUN", 660, 920, -232, 695},   /* Recipe 2 */
    {"CAN_330ML_SLK", 580, 1450, 298, 611},   /* Recipe 3 */
    {"CAN_500ML_CAO", 660, 1680, 528, 695},   /* Recipe 4 */
};

void recipe_factory_default(uint8_t id, uint16_t m6_ref_330_x10, CanRecipe_t *out)
{
    memset(out, 0, sizeof(*out));
    const DefaultDef_t *d = (id >= 1U && id <= 4U) ? &k_defaults[id - 1U] : &k_defaults[0];
    out->recipe_id = id;
    if (id >= 1U && id <= 4U) {
        strncpy(out->can_name, d->name, sizeof(out->can_name) - 1U);
    } else {
        memcpy(out->can_name, "RECIPE_", 7);   /* "RECIPE_05" .. "RECIPE_16" */
        out->can_name[7] = (char)('0' + id / 10U);
        out->can_name[8] = (char)('0' + id % 10U);
    }
    int32_t m6 = (int32_t)m6_ref_330_x10 + d->m6_rel_x10;
    out->can_diameter_x10     = d->dia_x10;
    out->can_height_x10       = d->h_x10;
    out->tab_to_label_offset  = 0;
    out->m6_height_pos_x10    = (uint16_t)((m6 < 0) ? 0 : m6);
    out->m2_angle_scale_x1000 = d->scale;
    out->belt_speed_cans_min  = 120;
    out->m4_overrun_pulses    = 50;
    out->p5_down_dwell_ms     = 40;
    out->vacuum_detect_ms     = 50;
    out->blow_off_time_ms     = 30;
    recipe_seal(out);
}

bool recipe_check_ranges(const CanRecipe_t *r)
{
    return r->recipe_id >= 1U && r->recipe_id <= 16U &&
           r->can_diameter_x10 >= 300U && r->can_diameter_x10 <= 1200U &&   /* 30..120 mm */
           r->can_height_x10 >= 500U && r->can_height_x10 <= 2500U &&       /* 50..250 mm */
           r->tab_to_label_offset >= -18000 && r->tab_to_label_offset <= 18000 &&
           r->m6_height_pos_x10 <= 980U &&
           r->m2_angle_scale_x1000 >= 100U && r->m2_angle_scale_x1000 <= 2000U &&
           r->belt_speed_cans_min >= 60U && r->belt_speed_cans_min <= 156U &&
           r->m4_overrun_pulses <= 200U &&
           r->p5_down_dwell_ms >= 20U && r->p5_down_dwell_ms <= 100U &&
           r->vacuum_detect_ms <= 200U && r->blow_off_time_ms <= 100U;
}
