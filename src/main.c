/**
 * @file main.c
 * @brief PMeMS CONTROLLER 1 - Can orientation & lid labeling line (Nam Viet F&B).
 *
 *  MCU : STM32F407VET6 @ 168 MHz
 *  SDK : STM32Cube HAL + FreeRTOS (CMSIS-RTOS v2)
 *  Spec: docs/FIRMWARE_PROCESS_SPEC.md REV 3.2
 *
 *  main() only performs the minimal pre-RTOS initialisation. Everything else is
 *  brought up by the "init" task (see app/app_main.c), because FreeRTOS masks
 *  interrupts from the first API call until the scheduler starts.
 */
#include "app_main.h"
#include "bsp_board.h"
#include "cmsis_os2.h"
#include "error_handler.h"
#include "stm32f4xx_hal.h"
#include "system_clock.h"

int main(void)
{
    HAL_Init();                 /* NVIC group 4, HAL timebase on TIM7 */
    system_clock_config();      /* 168 MHz */
    bsp_board_init();           /* GPIO safe states, KM1 off, M6 brake engaged */

    if (osKernelInitialize() != osOK) {
        error_handler();
    }
    app_create();
    (void)osKernelStart();

    /* never reached */
    error_handler();
}
