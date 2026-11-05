/**
 * @file error_handler.c
 * @brief Fatal error handling and FreeRTOS hooks. On any fatal error the machine
 *        is put in a safe state and the IWDG resets the MCU.
 */
#include "error_handler.h"

#include "FreeRTOS.h"
#include "board_pins.h"
#include "bsp_dio.h"
#include "io_map.h"
#include "task.h"

void error_handler(void)
{
    __disable_irq();
    /* KM1 off, M6 brake engaged (direct register writes, no HAL) */
    KM1_PORT->BSRR      = (uint32_t)KM1_PIN << 16U;
    M6_BRAKE_PORT->BSRR = (uint32_t)M6_BRAKE_PIN << 16U;
    for (uint8_t i = 0; i < DIO_DO_COUNT; i++) {
        bsp_dio_do_write(i, (DO_SAFE_IMAGE >> i) & 1U);
    }
    bsp_dio_do_commit();
    for (;;) {
        /* wait for IWDG reset */
    }
}

void app_rtos_assert_failed(const char *file, int line)
{
    (void)file;
    (void)line;
    error_handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    error_handler();
}

void vApplicationMallocFailedHook(void)
{
    error_handler();
}
