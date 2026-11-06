#include "recipe.h"

#include <string.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "bsp_eeprom.h"
#include "semphr.h"

static CanRecipe_t s_active;
static SysConfig_t s_cfg;
static SemaphoreHandle_t s_mutex;
static bool s_recipe_dirty, s_cfg_dirty, s_eeprom_ok;

static uint16_t recipe_addr(uint8_t id)
{
    return (uint16_t)(EEPROM_ADDR_RECIPE_BASE + (uint16_t)(id - 1U) * RECIPE_SIZE);
}

static void syscfg_defaults(SysConfig_t *c)
{
    static const uint8_t ip[4] = NET_IP_DEFAULT, mask[4] = NET_MASK_DEFAULT, gw[4] = NET_GW_DEFAULT;
    memset(c, 0, sizeof(*c));
    c->magic            = SYSCFG_MAGIC;
    c->version          = SYSCFG_VERSION;
    c->active_recipe_id = 1;
    memcpy(c->ip, ip, 4);
    memcpy(c->mask, mask, 4);
    memcpy(c->gw, gw, 4);
    syscfg_seal(c);
}

static bool load_recipe(uint8_t id, CanRecipe_t *out)
{
    if (s_eeprom_ok && bsp_eeprom_read(recipe_addr(id), out, sizeof(*out)) && recipe_valid(out) &&
        out->recipe_id == id) {
        return true;
    }
    recipe_factory_default(id, M6_REF_POS_330ML_X10, out);
    if (s_eeprom_ok) {
        (void)bsp_eeprom_write(recipe_addr(id), out, sizeof(*out));
    }
    return false;
}

void recipe_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_eeprom_ok = bsp_eeprom_init();

    if (!s_eeprom_ok || !bsp_eeprom_read(EEPROM_ADDR_SYSCFG, &s_cfg, sizeof(s_cfg)) || !syscfg_valid(&s_cfg)) {
        syscfg_defaults(&s_cfg);
        if (s_eeprom_ok) {
            (void)bsp_eeprom_write(EEPROM_ADDR_SYSCFG, &s_cfg, sizeof(s_cfg));
        }
    }
    if (s_cfg.active_recipe_id < 1U || s_cfg.active_recipe_id > RECIPE_COUNT) {
        s_cfg.active_recipe_id = 1;
    }
    (void)load_recipe(s_cfg.active_recipe_id, &s_active);
}

void recipe_get_active(CanRecipe_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_active;
    xSemaphoreGive(s_mutex);
}

uint8_t recipe_active_id(void) { return s_active.recipe_id; }

bool recipe_select(uint8_t id)
{
    if (id < 1U || id > RECIPE_COUNT) {
        return false;
    }
    CanRecipe_t r;
    (void)load_recipe(id, &r);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active = r;
    s_cfg.active_recipe_id = id;
    s_cfg_dirty = true;
    xSemaphoreGive(s_mutex);
    return true;
}

bool recipe_load(uint8_t id, CanRecipe_t *out)
{
    if (id < 1U || id > RECIPE_COUNT) {
        return false;
    }
    if (id == s_active.recipe_id) {
        recipe_get_active(out);    /* RAM copy may hold unsaved HMI edits */
        return true;
    }
    return load_recipe(id, out);
}

RecipeStoreResult_t recipe_store(const CanRecipe_t *r, bool *active_changed)
{
    CanRecipe_t c = *r;
    recipe_seal(&c);
    *active_changed = false;
    if (!recipe_check_ranges(&c)) {
        return RECIPE_STORE_RANGE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (c.recipe_id == s_active.recipe_id) {
        s_active = c;
        s_recipe_dirty = false;
        *active_changed = true;
    }
    xSemaphoreGive(s_mutex);
    bool ok = s_eeprom_ok && bsp_eeprom_write(recipe_addr(c.recipe_id), &c, sizeof(c));
    return ok ? RECIPE_STORE_OK : RECIPE_STORE_EEPROM;
}

bool recipe_update_active(const CanRecipe_t *r)
{
    if (!recipe_check_ranges(r) || r->recipe_id != s_active.recipe_id) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active = *r;
    recipe_seal(&s_active);
    s_recipe_dirty = true;
    xSemaphoreGive(s_mutex);
    return true;
}

void recipe_get_syscfg(SysConfig_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mutex);
}

void recipe_add_lifetime(uint32_t cans, uint32_t ok, uint32_t ng)
{
    if (cans == 0U && ok == 0U && ng == 0U) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cfg.lifetime_cans += cans;
    s_cfg.lifetime_ok   += ok;
    s_cfg.lifetime_ng   += ng;
    s_cfg_dirty = true;
    xSemaphoreGive(s_mutex);
}

void recipe_service(bool allow_write)
{
    if (!allow_write || !s_eeprom_ok || (!s_recipe_dirty && !s_cfg_dirty)) {
        return;
    }
    CanRecipe_t r;
    SysConfig_t c;
    bool wr_r, wr_c;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    r = s_active;
    syscfg_seal(&s_cfg);
    c = s_cfg;
    wr_r = s_recipe_dirty;
    wr_c = s_cfg_dirty;
    s_recipe_dirty = false;
    s_cfg_dirty = false;
    xSemaphoreGive(s_mutex);

    if (wr_r) {
        (void)bsp_eeprom_write(recipe_addr(r.recipe_id), &r, sizeof(r));
    }
    if (wr_c) {
        (void)bsp_eeprom_write(EEPROM_ADDR_SYSCFG, &c, sizeof(c));
    }
}

bool recipe_eeprom_ok(void) { return s_eeprom_ok; }
