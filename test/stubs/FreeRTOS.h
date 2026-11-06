/* Host stubs so that src/app/tracking.c (real firmware code) runs on a PC. */
#ifndef STUB_FREERTOS_H
#define STUB_FREERTOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configASSERT(x) ((void)(x))
#define portYIELD_FROM_ISR(x) ((void)(x))
extern TickType_t g_tick;
QueueHandle_t xQueueCreate(int n, size_t sz);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
BaseType_t xQueueReset(QueueHandle_t q);
static inline TickType_t xTaskGetTickCountFromISR(void) { return g_tick; }
#endif
