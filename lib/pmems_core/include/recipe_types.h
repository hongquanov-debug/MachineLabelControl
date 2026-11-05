/**
 * @file recipe_types.h
 * @brief Recipe (64 bytes) and system config layout stored in AT24C64 (Spec §8).
 *
 * NOTE: the field list of spec §8.1 sums to 61 bytes with reserved[22]; reserved
 * is extended to 25 bytes so that the struct is exactly 64 bytes as specified
 * (see docs/SPEC_NOTES.md). CRC16 Modbus covers the first 62 bytes.
 */
#ifndef RECIPE_TYPES_H
#define RECIPE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t  recipe_id;             /* 1 .. 16 */
    char     can_name[16];          /* e.g. "CAN_330ML_STD\0" */
    uint16_t can_diameter_x10;      /* 66.0 mm -> 660 */
    uint16_t can_height_x10;        /* 115.2 mm -> 1152 */
    int16_t  tab_to_label_offset;   /* deg x100 (-18000 .. +18000) */
    uint16_t m6_height_pos_x10;     /* absolute M6 position, 0.1 mm */
    uint16_t m2_angle_scale_x1000;  /* (D_can / 95.0) x 1000 */
    uint16_t belt_speed_cans_min;   /* cans/min */
    uint16_t m4_overrun_pulses;     /* M4 pulses after label gap */
    uint16_t p5_down_dwell_ms;      /* press time */
    uint16_t vacuum_detect_ms;      /* vacuum switch check delay */
    uint16_t blow_off_time_ms;      /* blow-off pulse */
    uint8_t  reserved[25];
    uint16_t crc16;                 /* CRC16 Modbus of bytes 0..61 */
} CanRecipe_t;

#define SYSCFG_MAGIC   0x504D4531UL  /* "PME1" */
#define SYSCFG_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  active_recipe_id;
    uint8_t  reserved0;
    uint32_t lifetime_cans;
    uint32_t lifetime_ok;
    uint32_t lifetime_ng;
    uint8_t  ip[4];
    uint8_t  mask[4];
    uint8_t  gw[4];
    uint8_t  reserved[30];
    uint16_t crc16;                 /* CRC16 Modbus of bytes 0..61 */
} SysConfig_t;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(CanRecipe_t) == 64, "CanRecipe_t must be 64 bytes");
static_assert(sizeof(SysConfig_t) == 64, "SysConfig_t must be 64 bytes");
#else
_Static_assert(sizeof(CanRecipe_t) == 64, "CanRecipe_t must be 64 bytes");
_Static_assert(sizeof(SysConfig_t) == 64, "SysConfig_t must be 64 bytes");
#endif

/** Compute and store CRC. */
void recipe_seal(CanRecipe_t *r);
bool recipe_valid(const CanRecipe_t *r);
void syscfg_seal(SysConfig_t *c);
bool syscfg_valid(const SysConfig_t *c);

/**
 * Factory defaults (Spec §8.2: recipes 1..4 defined, 5..16 = copy of 330 ml std).
 * @p m6_ref_330_x10 = absolute M6 axis position for the 330 ml standard can;
 * recipe heights are given relative to it in the spec.
 */
void recipe_factory_default(uint8_t id, uint16_t m6_ref_330_x10, CanRecipe_t *out);

/** Range check of user-editable values (HMI / SCADA writes). */
bool recipe_check_ranges(const CanRecipe_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_TYPES_H */
