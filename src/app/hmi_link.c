#include "hmi_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "bsp_rs485.h"
#include "modbus_regs.h"
#include "task.h"

static TaskHandle_t s_task;
static uint8_t s_rx[MB_RTU_MAX_ADU];
static volatile uint16_t s_rx_len;
static volatile bool s_overflow;

static void rx_cb(const uint8_t *data, uint16_t len, bool idle, void *ctx)
{
    (void)ctx;
    if (len > 0U) {
        if ((uint32_t)s_rx_len + len <= sizeof(s_rx)) {
            memcpy(&s_rx[s_rx_len], data, len);
            s_rx_len = (uint16_t)(s_rx_len + len);
        } else {
            s_overflow = true;
        }
    }
    if (idle && s_task != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void hmi_link_init(void)
{
    bsp_rs485_init(RS485_HMI, HMI_BAUD, RS485_PARITY_NONE, rx_cb, NULL);
}

void hmi_link_task(void *arg)
{
    (void)arg;
    static uint8_t req[MB_RTU_MAX_ADU];
    static uint8_t resp[MB_RTU_MAX_ADU];
    s_task = xTaskGetCurrentTaskHandle();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        taskENTER_CRITICAL();
        uint16_t len = s_rx_len;
        bool ovf = s_overflow;
        memcpy(req, s_rx, len);
        s_rx_len = 0;
        s_overflow = false;
        taskEXIT_CRITICAL();

        if (ovf || len == 0U) {
            continue;
        }
        size_t n = mb_rtu_slave_process(modbus_regs_map(), HMI_SLAVE_ID, req, len, resp, sizeof(resp));
        if (n > 0U) {
            (void)bsp_rs485_send(RS485_HMI, resp, (uint16_t)n, 200);
        }
    }
}
