/**
 * @file bsp_encoder.h
 * @brief Conveyor encoder on TIM2 (32-bit), TIM_ENCODERMODE_TI12, PA0/PA1 (Spec §1).
 */
#ifndef BSP_ENCODER_H
#define BSP_ENCODER_H

#include <stdint.h>

#include "stm32f4xx.h"

void bsp_encoder_init(void);

/** Raw 32-bit position (0.1 mm / count). Use unsigned subtraction for distances. */
static inline uint32_t bsp_encoder_now(void) { return TIM2->CNT; }

#endif
