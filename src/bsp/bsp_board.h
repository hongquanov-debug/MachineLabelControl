/**
 * @file bsp_board.h
 * @brief Board-level GPIO: safety chain, home sensors, camera trigger, us-delay (DWT).
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"

void bsp_board_init(void);

/* ---- microsecond delay (DWT cycle counter) ---- */
void bsp_delay_us(uint32_t us);
static inline uint32_t bsp_cycles(void) { return DWT->CYCCNT; }

/* ---- safety chain (true = healthy) ---- */
bool bsp_estop_ok(void);
bool bsp_door_closed(void);
bool bsp_air_ok(void);
bool bsp_safety_reset_pressed(void);

/* ---- power / actuators directly driven by MCU ---- */
void bsp_km1_set(bool on);
void bsp_m6_brake_release(bool release);  /* false = brake engaged (fail-safe) */
void bsp_buzzer_set(bool on);
void bsp_led_toggle(void);

/* ---- sensors ---- */
typedef enum { HOME_M3 = 0, HOME_M4, HOME_M5, HOME_M6A, HOME_M6B, HOME_COUNT } HomeSensor_t;
bool bsp_home_active(HomeSensor_t s);
bool bsp_s5_label_present(void);

/* ---- camera trigger: 10 us pulse on CAM_TRIG (ISR safe) ---- */
void bsp_cam_trigger_pulse(void);

#endif /* BSP_BOARD_H */
