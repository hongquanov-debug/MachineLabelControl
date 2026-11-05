/**
 * @file crit.h
 * @brief Nestable critical section usable from tasks, ISRs and before the
 *        scheduler starts. Masks interrupts with priority >= configMAX_SYSCALL
 *        (5..15) via BASEPRI; high-priority pulse ISRs (0..4) keep running.
 */
#ifndef CRIT_H
#define CRIT_H

#include <stdint.h>

#include "FreeRTOSConfig.h"
#include "stm32f4xx.h"

static inline uint32_t crit_enter(void)
{
    uint32_t old = __get_BASEPRI();
    __set_BASEPRI_MAX(configMAX_SYSCALL_INTERRUPT_PRIORITY);
    __DSB();
    __ISB();
    return old;
}

static inline void crit_exit(uint32_t old)
{
    __set_BASEPRI(old);
}

#endif /* CRIT_H */
