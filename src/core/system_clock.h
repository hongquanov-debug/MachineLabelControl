#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

/** HSE 8 MHz -> PLL -> SYSCLK 168 MHz, AHB 168, APB1 42 (TIM x2 = 84), APB2 84 (TIM x2 = 168). */
void system_clock_config(void);

#endif
