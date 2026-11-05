#include "bsp_eeprom.h"

#include "FreeRTOS.h"
#include "app_config.h"
#include "board_pins.h"
#include "semphr.h"
#include "task.h"

static I2C_HandleTypeDef s_i2c;
static SemaphoreHandle_t s_mutex;

#define I2C_TIMEOUT_MS   20U
#define WRITE_CYCLE_MS   5U

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

    GPIO_InitTypeDef g = {0};
    g.Pin       = EEPROM_SCL_PIN | EEPROM_SDA_PIN;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(EEPROM_SCL_PORT, &g);

    s_i2c.Instance             = I2C1;
    s_i2c.Init.ClockSpeed      = 400000;
    s_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_i2c.Init.OwnAddress1     = 0;
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
    return HAL_I2C_IsDeviceReady(&s_i2c, EEPROM_I2C_ADDR, 3, I2C_TIMEOUT_MS) == HAL_OK;
}

bool bsp_eeprom_read(uint16_t addr, void *buf, uint16_t len)
{
    if ((uint32_t)addr + len > EEPROM_SIZE) {
        return false;
    }
    lock();
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&s_i2c, EEPROM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                                           (uint8_t *)buf, len, I2C_TIMEOUT_MS + len / 4U);
    unlock();
    return st == HAL_OK;
}

static bool wait_ready(void)
{
    for (int i = 0; i < 10; i++) {
        if (HAL_I2C_IsDeviceReady(&s_i2c, EEPROM_I2C_ADDR, 1, 2) == HAL_OK) {
            return true;
        }
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
    while (len > 0U && ok) {
        uint16_t room  = (uint16_t)(EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE));
        uint16_t chunk = (len < room) ? len : room;
        ok = HAL_I2C_Mem_Write(&s_i2c, EEPROM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)(uintptr_t)p, chunk, I2C_TIMEOUT_MS) == HAL_OK;
        ok = ok && wait_ready();   /* internal write cycle ~5 ms */
        addr = (uint16_t)(addr + chunk);
        p += chunk;
        len = (uint16_t)(len - chunk);
    }
    unlock();
    return ok;
}
