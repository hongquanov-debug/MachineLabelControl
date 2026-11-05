/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS kernel configuration - PMeMS Controller 1 (STM32F407, Cortex-M4F).
 *
 * Interrupt priority policy (NVIC, 4 bits, NVIC_PRIORITYGROUP_4):
 *   0..4  : "Hard real-time" ISRs - MUST NOT call FreeRTOS API
 *           (pulse generator timers M2..M6 = 2)
 *   5     : configMAX_SYSCALL - Can tracking (EXTI2 S2, TIM6 5 kHz)
 *   6..14 : UART / DMA (RS485_x), I2C...
 *   15    : Kernel (PendSV, SysTick), HAL timebase TIM7
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
void app_rtos_assert_failed(const char *file, int line);
#endif

#define configUSE_PREEMPTION                     1
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       (SystemCoreClock)
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     (56)   /* required by CMSIS-RTOS v2 */
#define configMINIMAL_STACK_SIZE                 ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                    ((size_t)(48 * 1024))
#define configMAX_TASK_NAME_LEN                  (16)
#define configUSE_TRACE_FACILITY                 1
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                        1
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TASK_NOTIFICATIONS             1
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configRECORD_STACK_HIGH_ADDRESS          1
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t

/* Co-routines */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          (2)

/* Software timers */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                (2)
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256

/* CMSIS-RTOS v2 options */
#define CMSIS_device_header "stm32f4xx.h"
#define USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION 0  /* cmsis_os2.c provides SysTick_Handler */

/* Optional functions */
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskCleanUpResources            0
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTimerPendFunctionCall           1
#define INCLUDE_xQueueGetMutexHolder             1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_eTaskGetState                    1

/* Cortex-M interrupt priorities */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                          __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                          4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x)                                   \
    do {                                                  \
        if ((x) == 0) {                                   \
            app_rtos_assert_failed(__FILE__, __LINE__);   \
        }                                                 \
    } while (0)

/* Map FreeRTOS port handlers to CMSIS names (SysTick handled in cmsis_os2.c) */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
