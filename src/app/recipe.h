/**
 * @file recipe.h
 * @brief Recipe & system configuration persistence in AT24C64 (Spec §8).
 */
#ifndef RECIPE_H
#define RECIPE_H

#include <stdbool.h>
#include <stdint.h>

#include "recipe_types.h"

/** Load system config + active recipe; writes factory defaults on blank/corrupt EEPROM. */
void recipe_init(void);

/** Copy of the active recipe (thread-safe). */
void recipe_get_active(CanRecipe_t *out);
uint8_t recipe_active_id(void);

/** Select and load recipe @p id (1..16), persisted as last active. */
bool recipe_select(uint8_t id);

/** Modify the active recipe in RAM (validated); persisted by recipe_service(). */
bool recipe_update_active(const CanRecipe_t *r);

/** Periodic (machine task): writes dirty data to EEPROM when allowed. */
void recipe_service(bool allow_write);

/** System config (network settings, lifetime counters). */
void recipe_get_syscfg(SysConfig_t *out);
void recipe_add_lifetime(uint32_t cans, uint32_t ok, uint32_t ng);

bool recipe_eeprom_ok(void);

#endif /* RECIPE_H */
