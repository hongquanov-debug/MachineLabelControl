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

/** Read recipe @p id (EEPROM, or factory default if blank/corrupt). false if defaulted. */
bool recipe_load(uint8_t id, CanRecipe_t *out);

/**
 * Validate, seal and write a recipe to EEPROM immediately. If it is the active
 * recipe the RAM copy is updated too (@p active_changed = true).
 * Returns false on range error (nothing changed) or EEPROM error (RAM updated).
 */
typedef enum { RECIPE_STORE_OK = 0, RECIPE_STORE_RANGE, RECIPE_STORE_EEPROM } RecipeStoreResult_t;
RecipeStoreResult_t recipe_store(const CanRecipe_t *r, bool *active_changed);

/** Modify the active recipe in RAM (validated); persisted by recipe_service(). */
bool recipe_update_active(const CanRecipe_t *r);

/** Periodic (machine task): writes dirty data to EEPROM when allowed. */
void recipe_service(bool allow_write);

/** System config (network settings, lifetime counters). */
void recipe_get_syscfg(SysConfig_t *out);
void recipe_add_lifetime(uint32_t cans, uint32_t ok, uint32_t ng);

bool recipe_eeprom_ok(void);

#endif /* RECIPE_H */
