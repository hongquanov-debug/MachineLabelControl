/**
 * @file bsp_eeprom.c
 * @brief EEPROM AT24C64 (8 KB) trên I2C1 - lưu recipe và cấu hình (Spec Mục 8).
 *
 *  I2C: bus 2 dây (SCL = xung nhịp, SDA = dữ liệu), nhiều thiết bị dùng chung,
 *  mỗi thiết bị có một địa chỉ (AT24C64: 0xA0). Các chân I2C là "open-drain":
 *  chỉ kéo xuống 0, mức 1 có được nhờ điện trở kéo lên.
 *
 *  Đặc điểm AT24C64 cần biết:
 *  - Địa chỉ ô nhớ 16 bit (0x0000..0x1FFF).
 *  - Ghi theo TRANG 32 byte: một lần ghi không được vượt qua ranh giới trang,
 *    nếu vượt sẽ quay vòng về đầu trang và ghi đè dữ liệu cũ!
 *  - Sau mỗi lần ghi, chip cần ~5 ms để ghi vào ô nhớ, trong lúc đó nó
 *    không trả lời trên bus (dùng HAL_I2C_IsDeviceReady() để chờ).
 *
 *  Đây là ví dụ HAL ở chế độ "blocking" (chặn): HAL_I2C_Mem_Read() chỉ trả về
 *  khi xong hoặc hết thời gian (timeout tính bằng HAL_GetTick()). Đơn giản,
 *  phù hợp vì EEPROM chỉ đọc/ghi thỉnh thoảng.
 *
 *  Mutex: nhiều task (machine, hmi, net) có thể cùng truy cập EEPROM. Mutex
 *  đảm bảo tại một thời điểm chỉ một task dùng bus I2C.
 */
#include "bsp_eeprom.h"

#include "FreeRTOS.h"
#include "app_config.h"
#include "board_pins.h"
#include "semphr.h"
#include "task.h"

static I2C_HandleTypeDef s_i2c;     /* Handle HAL của I2C1 */
static SemaphoreHandle_t s_mutex;

#define I2C_TIMEOUT_MS   20U
#define WRITE_CYCLE_MS   5U

/* Chỉ dùng mutex khi scheduler đã chạy (trước đó chỉ có một luồng). */
static void lock(void)
{
    if (s_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(s_mutex);
    }
}

bool bsp_eeprom_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* PB8 = SCL, PB9 = SDA ở chế độ Alternate Function OPEN-DRAIN (bắt buộc với I2C) */
    GPIO_InitTypeDef g = {0};
    g.Pin       = EEPROM_SCL_PIN | EEPROM_SDA_PIN;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(EEPROM_SCL_PORT, &g);

    s_i2c.Instance             = I2C1;
    s_i2c.Init.ClockSpeed      = 400000;                  /* Fast mode 400 kHz */
    s_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_i2c.Init.OwnAddress1     = 0;                       /* MCU là master, không cần địa chỉ */
    s_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&s_i2c) != HAL_OK) {
        return false;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    /* Hỏi thử chip 3 lần: có trả lời (ACK) nghĩa là EEPROM đang có mặt trên bus. */
    return HAL_I2C_IsDeviceReady(&s_i2c, EEPROM_I2C_ADDR, 3, I2C_TIMEOUT_MS) == HAL_OK;
}

bool bsp_eeprom_read(uint16_t addr, void *buf, uint16_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) {
        return false;
    }
    lock();
    /* HAL_I2C_Mem_Read: gửi địa chỉ chip + địa chỉ ô nhớ (16 bit) rồi đọc len byte.
     * Timeout tăng theo độ dài (~25 us/byte ở 400 kHz). Đọc không bị giới hạn trang. */
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&s_i2c, EEPROM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                                           (uint8_t *)buf, len, I2C_TIMEOUT_MS + len / 4U);
    unlock();
    return st == HAL_OK;
}

/** Chờ chip ghi xong (thường ~5 ms, tối đa 10 lần thử). */
static bool wait_ready(void)
{
    for (int i = 0; i < 10; i++) {
        if (HAL_I2C_IsDeviceReady(&s_i2c, EEPROM_I2C_ADDR, 1, 2) == HAL_OK) {
            return true;
        }
        /* Có RTOS: vTaskDelay nhường CPU cho task khác. Chưa có: HAL_Delay chờ bận. */
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            HAL_Delay(1);
        }
    }
    return false;
}

bool bsp_eeprom_write(uint16_t addr, const void *buf, uint16_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) {
        return false;
    }
    const uint8_t *p = (const uint8_t *)buf;
    bool ok = true;
    lock();
    /* Chia dữ liệu thành các khúc không vượt ranh giới trang 32 byte.
     * Ví dụ ghi 64 byte từ địa chỉ 0x0050: khúc 1 = 0x50..0x5F (16 byte),
     * khúc 2 = 0x60..0x7F (32 byte), khúc 3 = 0x80..0x8F (16 byte). */
    while (len > 0U && ok) {
        uint16_t room  = (uint16_t)(EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE));  /* chỗ trống tới cuối trang */
        uint16_t chunk = (len < room) ? len : room;
        ok = HAL_I2C_Mem_Write(&s_i2c, EEPROM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)(uintptr_t)p, chunk, I2C_TIMEOUT_MS) == HAL_OK;
        ok = ok && wait_ready();   /* chờ chu kỳ ghi nội bộ ~5 ms */
        addr = (uint16_t)(addr + chunk);
        p += chunk;
        len = (uint16_t)(len - chunk);
    }
    unlock();
    return ok;
}
