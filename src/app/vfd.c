#include "vfd.h"

#include <string.h>

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_rs485.h"
#include "modbus_pdu.h"
#include "task.h"

#define MAX_CONSEC_ERRORS 10U

static TaskHandle_t s_task;
static uint8_t s_rx[MB_RTU_MAX_ADU];
static volatile uint16_t s_rx_len;
static volatile bool s_run_req;
static volatile uint16_t s_freq_req;
static VfdStatus_t s_st;

static void rx_cb(const uint8_t *data, uint16_t len, bool idle, void *ctx)
{
    (void)ctx;
    if (len > 0U && (uint32_t)s_rx_len + len <= sizeof(s_rx)) {
        memcpy(&s_rx[s_rx_len], data, len);
        s_rx_len = (uint16_t)(s_rx_len + len);
    }
    if (idle && s_task != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void vfd_init(void)
{
    bsp_rs485_init(RS485_VFD, VFD_BAUD, RS485_PARITY_EVEN, rx_cb, NULL);
}

void vfd_run(uint16_t freq_x100)
{
    s_freq_req = freq_x100;
    s_run_req  = true;
}

void vfd_stop(void) { s_run_req = false; }

VfdStatus_t vfd_status(void) { return s_st; }

uint16_t vfd_freq_for_speed(uint16_t cans_per_min)
{
    float mm_s = (float)cans_per_min * (float)CAN_PITCH_MM / 60.0f;
    float hz = mm_s / BELT_MM_S_PER_HZ;
    if (hz > 60.0f) {
        hz = 60.0f;
    }
    return (uint16_t)(hz * 100.0f);
}

/** Send request, wait for the response frame. Returns response length or 0. */
static uint16_t transact(const uint8_t *req, uint16_t len)
{
    s_rx_len = 0;
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (!bsp_rs485_send(RS485_VFD, req, len, 50)) {
        return 0;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VFD_RESP_TIMEOUT_MS)) == 0U) {
        return 0;
    }
    return s_rx_len;
}

static bool write_reg(uint16_t reg, uint16_t val)
{
    uint8_t req[8];
    uint16_t n = (uint16_t)mb_rtu_build_write_single(VFD_SLAVE_ID, reg, val, req);
    uint16_t r = transact(req, n);
    return r > 0U && mb_rtu_parse_write_single_resp(VFD_SLAVE_ID, reg, val, s_rx, r) == 0;
}

void vfd_task(void *arg)
{
    (void)arg;
    s_task = xTaskGetCurrentTaskHandle();
    uint32_t consec_err = 0;
    bool last_run = false;
    uint16_t last_freq = 0xFFFF;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        bool ok = true;
        bool run = s_run_req;
        uint16_t freq = s_freq_req;

        /* setpoint first, then run command (only on change or comm recovery) */
        if (run && (freq != last_freq || !s_st.online)) {
            if (write_reg(VFD_REG_FREQ, freq)) {
                last_freq = freq;
                s_st.freq_set_x100 = freq;
            } else {
                ok = false;
            }
        }
        if (ok && (run != last_run || !s_st.online)) {
            if (write_reg(VFD_REG_CMD, run ? VFD_CMD_RUN_FWD : VFD_CMD_COAST_STOP)) {
                last_run = run;
                s_st.running_cmd = run;
            } else {
                ok = false;
            }
        }

        /* monitoring: 0x2100 .. 0x2104 */
        uint8_t req[8];
        uint16_t regs[5];
        uint16_t n = (uint16_t)mb_rtu_build_read(VFD_SLAVE_ID, MB_FC_READ_HOLDING, VFD_REG_FAULT, 5, req);
        uint16_t r = transact(req, n);
        if (r > 0U && mb_rtu_parse_read_resp(VFD_SLAVE_ID, MB_FC_READ_HOLDING, s_rx, r, regs, 5) == 0) {
            s_st.fault_code    = regs[0];
            s_st.out_freq_x100 = regs[2];
            s_st.current_x10   = regs[4];
            if (s_st.fault_code != 0U) {
                alarm_raise(ALM_17_VFD_FAULT);
            }
        } else {
            ok = false;
        }

        if (ok) {
            consec_err = 0;
            s_st.online = true;
        } else {
            s_st.comm_errors++;
            if (++consec_err >= MAX_CONSEC_ERRORS) {
                s_st.online = false;
                alarm_raise(ALM_19_VFD_COMM_LOST);
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(VFD_POLL_MS));
    }
}
