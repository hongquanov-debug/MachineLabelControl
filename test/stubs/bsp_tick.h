#include <stdint.h>
typedef void (*bsp_tick_cb_t)(void);
void bsp_tick_init(uint32_t, bsp_tick_cb_t);
