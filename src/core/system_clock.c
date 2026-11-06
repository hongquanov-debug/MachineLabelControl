/**
 * @file system_clock.c
 * @brief Cấu hình cây xung nhịp (clock tree) của STM32F407.
 *
 *  Sơ đồ xung nhịp sau khi cấu hình:
 *
 *    HSE 8 MHz (thạch anh ngoài)
 *       │  /PLLM (8)   -> 1 MHz   (đầu vào PLL nên nằm trong 1..2 MHz)
 *       │  xPLLN (336) -> 336 MHz (VCO)
 *       ├─ /PLLP (2)   -> 168 MHz = SYSCLK (xung nhịp CPU)
 *       └─ /PLLQ (7)   ->  48 MHz (dành cho USB/SDIO, chưa dùng)
 *
 *    SYSCLK 168 MHz ─ /1 ─> HCLK (AHB)  168 MHz : CPU, DMA, GPIO
 *                     ├─ /4 ─> PCLK1 (APB1)  42 MHz : USART2/3, UART4, I2C1, SPI2, TIM2..7
 *                     │                               (timer APB1 chạy x2 = 84 MHz)
 *                     └─ /2 ─> PCLK2 (APB2)  84 MHz : TIM1, TIM8, TIM9
 *                                                     (timer APB2 chạy x2 = 168 MHz)
 *
 *  Lưu ý cho người mới: các bộ timer nhận xung = 2 x PCLK khi bộ chia APB khác 1.
 *  Vì vậy khi tính prescaler cho timer phải dùng 84 MHz / 168 MHz chứ không phải 42 / 84.
 */
#include "system_clock.h"

#include "error_handler.h"
#include "stm32f4xx_hal.h"

void system_clock_config(void)
{
    /* Kiểu dùng HAL điển hình: khai báo một "Init struct", điền các trường,
     * rồi truyền con trỏ vào hàm HAL_xxx_Config/Init. Gán = {0} để các trường
     * không dùng tới mang giá trị 0 (tránh rác trên stack).                  */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Muốn chạy 168 MHz thì bộ ổn áp lõi phải ở chế độ Scale 1 (hiệu năng cao).
     * Macro __HAL_RCC_xxx_CLK_ENABLE() bật xung cho một ngoại vi. Trên STM32,
     * MỌI ngoại vi đều tắt xung sau reset - quên bật xung thì ghi thanh ghi
     * của ngoại vi đó sẽ không có tác dụng. Đây là lỗi hay gặp nhất của người mới. */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* ---- Bước 1: bật các nguồn dao động và PLL ---- */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState       = RCC_HSE_ON;            /* thạch anh ngoài */
    osc.LSIState       = RCC_LSI_ON;            /* dao động nội ~32 kHz cho watchdog IWDG */
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = HSE_VALUE / 1000000U;  /* 8 MHz / 8 = 1 MHz (HSE_VALUE định nghĩa trong stm32f4xx_hal_conf.h) */
    osc.PLL.PLLN       = 336;                   /* 1 MHz x 336 = 336 MHz */
    osc.PLL.PLLP       = RCC_PLLP_DIV2;         /* 336 / 2 = 168 MHz */
    osc.PLL.PLLQ       = 7;                     /* 336 / 7 = 48 MHz */
    /* Hầu hết hàm HAL trả về HAL_StatusTypeDef: HAL_OK / HAL_ERROR / HAL_BUSY / HAL_TIMEOUT.
     * Luôn kiểm tra giá trị trả về - ở đây nếu thạch anh không dao động thì sẽ báo lỗi. */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_handler();
    }

    /* ---- Bước 2: chọn PLL làm xung hệ thống và đặt các bộ chia bus ---- */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;       /* HCLK  = 168 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;         /* PCLK1 = 42 MHz (tối đa cho phép 42 MHz) */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;         /* PCLK2 = 84 MHz (tối đa cho phép 84 MHz) */
    /* FLASH_LATENCY_5: Flash không đọc kịp ở 168 MHz nên CPU phải chờ 5 chu kỳ
     * mỗi lần đọc (bảng "wait states" trong Reference Manual RM0090).
     * HAL_RCC_ClockConfig() cũng tự gọi lại HAL_InitTick() để nhịp 1 ms đúng
     * với xung nhịp mới.                                                     */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        error_handler();
    }
}
