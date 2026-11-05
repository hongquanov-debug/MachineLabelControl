/**
 * @file app_main.h
 * @brief Application bootstrap: creates the init task which brings up all
 *        peripherals/modules and then starts the application tasks.
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

/** Called from main() after HAL/clock init, before osKernelStart(). */
void app_create(void);

#endif
