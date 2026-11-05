/**
 * @file bsp_tick.h
 * @brief TIM6 periodic interrupt (TRACKING_TICK_HZ) for the can tracking scan.
 */
#ifndef BSP_TICK_H
#define BSP_TICK_H

#include <stdint.h>

typedef void (*bsp_tick_cb_t)(void);

/** Start TIM6 at @p hz; @p cb runs in ISR context (NVIC priority 5). */
void bsp_tick_init(uint32_t hz, bsp_tick_cb_t cb);
void bsp_tick_irq(void);

#endif
