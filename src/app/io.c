/**
 * @file io.c
 * @brief Task I/O 1 ms - "trái tim" đọc/ghi tín hiệu của máy.
 *
 *  Mỗi 1 ms task này:
 *   1. Đọc 32 ngõ vào qua 74HCT165, kiểm tra tuột cáp (byte thứ 5 = 0xFF -> ALM_20).
 *   2. Đọc chuỗi an toàn (E-Stop, cửa, khí nén). E-Stop -> cắt KM1 NGAY tại đây,
 *      không chờ máy trạng thái (machine.c) xử lý.
 *   3. Kiểm tra tín hiệu Alarm của các driver, cảm biến hết tem.
 *   4. Xuất ảnh 24 ngõ ra ra 74HCT595.
 *
 *  Các module khác KHÔNG đọc phần cứng trực tiếp mà gọi di(DI_xxx) / do_set(DO_xxx, on).
 *  Lợi ích: một nơi duy nhất truy cập phần cứng, dễ giả lập và gỡ lỗi.
 *
 *  vTaskDelayUntil(&last, 1 ms): chạy đúng chu kỳ 1 ms tính từ lần thức trước,
 *  không bị trôi dần như vTaskDelay(1) (vốn tính từ lúc gọi).
 */
#include "io.h"

#include "FreeRTOS.h"
#include "alarm.h"
#include "app_config.h"
#include "bsp_board.h"
#include "bsp_dio.h"
#include "task.h"
#include "tracking.h"

static volatile uint32_t s_di;
static volatile SafetyStatus_t s_safety;
static volatile bool s_drives_powered;
static volatile uint32_t s_heartbeat;
static uint32_t s_powered_since;

void io_init(void)
{
    bsp_dio_init(DO_SAFE_IMAGE);
    s_di = 0;
    s_safety.di_bus_ok = true;
}

bool di(DiBit_t bit)        { return (s_di >> bit) & 1U; }
uint32_t di_image(void)     { return s_di; }
void do_set(DoBit_t bit, bool on) { bsp_dio_do_write((uint8_t)bit, on); }
bool do_get(DoBit_t bit)    { return bsp_dio_do_get((uint8_t)bit); }
uint32_t do_image(void)     { return bsp_dio_do_image(); }
void do_commit_now(void)    { bsp_dio_do_commit(); }

void io_outputs_safe(void)
{
    for (uint8_t i = 0; i < DIO_DO_COUNT; i++) {
        bsp_dio_do_write(i, (DO_SAFE_IMAGE >> i) & 1U);
    }
    bsp_dio_do_commit();
}

bool io_wait_di(DiBit_t bit, bool level, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    while (di(bit) != level) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

SafetyStatus_t io_safety(void)
{
    SafetyStatus_t s;
    s.estop_ok      = s_safety.estop_ok;
    s.door_closed   = s_safety.door_closed;
    s.air_ok        = s_safety.air_ok;
    s.reset_pressed = s_safety.reset_pressed;
    s.di_bus_ok     = s_safety.di_bus_ok;
    return s;
}

void io_set_drives_powered(bool powered)
{
    if (powered && !s_drives_powered) {
        s_powered_since = xTaskGetTickCount();
    }
    s_drives_powered = powered;
}

uint32_t io_heartbeat(void) { return s_heartbeat; }

static void check_driver_alarms(uint32_t img)
{
    /* allow drivers 500 ms to boot after power-up */
    if (!s_drives_powered || (xTaskGetTickCount() - s_powered_since) < pdMS_TO_TICKS(500)) {
        return;
    }
    if (img & DI_BIT(DI_ALM_M2)) alarm_raise(ALM_10_DRIVER_M2_FAULT);
    if (img & DI_BIT(DI_ALM_M3)) alarm_raise(ALM_11_SERVO_M3_FAULT);
    if (img & DI_BIT(DI_ALM_M4)) alarm_raise(ALM_12_DRIVER_M4_FAULT);
    if (img & DI_BIT(DI_ALM_M5)) alarm_raise(ALM_13_DRIVER_M5_FAULT);
    if (img & (DI_BIT(DI_ALM_M6A) | DI_BIT(DI_ALM_M6B))) {
        /* ALM_14: engage brake and cut driver power immediately (anti-drop) */
        bsp_m6_brake_release(false);
        bsp_dio_do_write(DO_ENA_M6, true);
        alarm_raise(ALM_14_DRIVER_M6_FAULT);
    }
}

void io_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    uint8_t bus_fault_cnt = 0;

    for (;;) {
        /* ---- 1. Inputs ---- */
        uint32_t raw;
        uint8_t extra;
        bsp_dio_scan(&raw, &extra);

        if (extra == 0xFFU) {
            if (bus_fault_cnt < DI_BUS_FAULT_SCANS) {
                bus_fault_cnt++;
            }
        } else {
            bus_fault_cnt = 0;
        }
        bool bus_ok = bus_fault_cnt < DI_BUS_FAULT_SCANS;
        if (bus_ok) {
            s_di = raw ^ DI_INVERT_MASK;
        } else {
            alarm_raise(ALM_20_LI32_BUS_DISCONNECT);  /* keep last valid image */
        }

        /* ---- 2. Safety chain (direct MCU inputs) ---- */
        s_safety.estop_ok      = bsp_estop_ok();
        s_safety.door_closed   = bsp_door_closed();
        s_safety.air_ok        = bsp_air_ok();
        s_safety.reset_pressed = bsp_safety_reset_pressed();
        s_safety.di_bus_ok     = bus_ok;

        if (!s_safety.estop_ok) {
            bsp_km1_set(false);                      /* ALM_01: cut KM1 immediately */
            bsp_m6_brake_release(false);
            alarm_raise(ALM_01_ESTOP_ACTIVATED);
        }
        if (!s_safety.door_closed) {
            alarm_raise(ALM_02_SAFETY_DOOR_OPEN);    /* FSM decides KM1 (only while running) */
        }
        if (!s_safety.air_ok) {
            alarm_raise(ALM_03_AIR_PRESSURE_LOW);
        }
        if (bus_ok) {
            check_driver_alarms(s_di);
            if (s_di & DI_BIT(DI_S14_TAPE_END)) {
                alarm_raise(ALM_07_LABEL_TAPE_EMPTY);
            }
        }

        /* ---- 3. Station services needing 1 ms resolution ---- */
        tracking_service_1ms();

        /* ---- 4. Outputs ---- */
        bsp_dio_do_commit();

        s_heartbeat++;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
    }
}
