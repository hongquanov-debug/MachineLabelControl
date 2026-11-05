/**
 * @file bsp_pulse.h
 * @brief Hardware pulse/direction generator for the 5 step/servo axes (Spec §4).
 *
 *   AXIS_M2  TIM1_CH1 PE9   (APB2 168 MHz)
 *   AXIS_M3  TIM8_CH1 PC6   (APB2 168 MHz)
 *   AXIS_M4  TIM3_CH1 PA6   (APB1  84 MHz)
 *   AXIS_M5  TIM4_CH1 PD12  (APB1  84 MHz)
 *   AXIS_M6  TIM9_CH1 PE5   (APB2 168 MHz, drives M6a + M6b in parallel)
 *
 * All timers are prescaled to PULSE_TIMER_TICK_HZ (10.5 MHz). Each period is
 * one pulse (PWM mode 2: low then high). The update ISR (NVIC prio 2, no RTOS
 * calls) counts pulses and reloads ARR from the trapezoidal ramp.
 */
#ifndef BSP_PULSE_H
#define BSP_PULSE_H

#include <stdbool.h>
#include <stdint.h>

#include "pulse_ramp.h"

typedef enum {
    AXIS_M2 = 0,   /* can orientation (PU wheel) */
    AXIS_M3,       /* 6-head rotary table (AC servo) */
    AXIS_M4,       /* label pulling */
    AXIS_M5,       /* lane shift */
    AXIS_M6,       /* lift (M6a + M6b) */
    AXIS_COUNT
} Axis_t;

void bsp_pulse_init(void);

/**
 * Start a move. @p pulses = 0 -> continuous (jog / homing search).
 * @p forward selects DIR level. Returns false if the axis is busy.
 */
bool bsp_pulse_start(Axis_t ax, bool forward, uint32_t pulses, const PulseProfile_t *prof);

/** Decelerated stop. */
void bsp_pulse_stop(Axis_t ax);

/** Immediate stop (E-Stop / alarm). */
void bsp_pulse_abort(Axis_t ax);

/** Convert running move into "stop after @p extra pulses" (sensor + overrun). */
void bsp_pulse_finish_after(Axis_t ax, uint32_t extra);

bool    bsp_pulse_busy(Axis_t ax);
int32_t bsp_pulse_position(Axis_t ax);           /**< absolute pulse counter */
void    bsp_pulse_set_position(Axis_t ax, int32_t pos);

void bsp_pulse_irq(Axis_t ax);

#endif /* BSP_PULSE_H */
