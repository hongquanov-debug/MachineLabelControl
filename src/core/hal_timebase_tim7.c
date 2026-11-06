/**
 * @file hal_timebase_tim7.c
 * @brief Nhịp thời gian 1 ms của HAL ("HAL tick") chạy trên TIM7.
 *
 *  HAL cần một bộ đếm mili-giây (biến uwTick) cho HAL_GetTick(), HAL_Delay()
 *  và cho timeout bên trong các hàm như HAL_I2C_Mem_Read(..., timeout).
 *  Mặc định HAL dùng SysTick, nhưng SysTick đã được FreeRTOS dùng làm nhịp
 *  của hệ điều hành. Vì vậy ta chuyển HAL tick sang TIM7 (giống CubeMX khi
 *  chọn "Timebase Source = TIM7").
 *
 *  Cơ chế "weak function" của HAL:
 *  HAL khai báo sẵn HAL_InitTick(), HAL_SuspendTick(), HAL_ResumeTick() với
 *  thuộc tính __weak (bản mặc định). Khi ta viết một hàm TRÙNG TÊN ở đây,
 *  trình liên kết (linker) dùng bản của ta thay cho bản mặc định. Đây là
 *  cách HAL cho phép người dùng "ghi đè" hành vi mà không sửa code thư viện.
 *  Các hàm callback như HAL_UART_TxCpltCallback() cũng dùng cơ chế này.
 */
#include "stm32f4xx_hal.h"

/**
 * Được HAL gọi trong HAL_Init() và mỗi lần đổi xung nhịp (HAL_RCC_ClockConfig).
 * @param TickPriority mức ưu tiên ngắt (TICK_INT_PRIORITY = 15, thấp nhất).
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    __HAL_RCC_TIM7_CLK_ENABLE();   /* bật xung cho TIM7 trước khi ghi thanh ghi */

    /* TIM7 nằm trên bus APB1. Xung vào timer = 2 x PCLK1 nếu bộ chia APB1 khác 1
     * (xem sơ đồ trong system_clock.c). Tính lại mỗi lần vì hàm này được gọi cả
     * lúc chip còn chạy 16 MHz lẫn sau khi đã lên 168 MHz.                      */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV1) ? pclk1 : 2U * pclk1;

    /* Ở đây ghi THẲNG vào thanh ghi thay vì dùng HAL_TIM_Base_Init() cho gọn.
     * TIMx->XXX là cách truy cập thanh ghi qua struct do CMSIS định nghĩa
     * (file stm32f407xx.h). Tên thanh ghi trùng với Reference Manual:
     *   CR1  : điều khiển (CEN = cho phép đếm)
     *   PSC  : bộ chia trước -> tần số đếm = timclk / (PSC + 1)
     *   ARR  : giá trị nạp lại -> tràn sau (ARR + 1) lần đếm
     *   EGR  : UG = nạp ngay PSC/ARR vào thanh ghi bóng (shadow)
     *   SR   : cờ trạng thái (UIF = cờ tràn)
     *   DIER : cho phép ngắt (UIE = ngắt khi tràn)                            */
    TIM7->CR1  = 0;
    TIM7->PSC  = (timclk / 1000000U) - 1U;                         /* đếm 1 MHz (1 us / lần) */
    TIM7->ARR  = (1000000U / (1000U / (uint32_t)uwTickFreq)) - 1U; /* 1000 lần đếm = 1 ms */
    TIM7->EGR  = TIM_EGR_UG;
    TIM7->SR   = 0;                                                /* xóa cờ do UG sinh ra */
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1  = TIM_CR1_CEN;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }
    /* NVIC = bộ điều khiển ngắt của lõi ARM. Hai bước luôn phải làm:
     * đặt mức ưu tiên (số càng NHỎ càng ưu tiên CAO) rồi cho phép ngắt.  */
    HAL_NVIC_SetPriority(TIM7_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    uwTickPrio = TickPriority;
    return HAL_OK;
}

/* HAL gọi 2 hàm này trước/sau khi vào chế độ ngủ (sleep). */
void HAL_SuspendTick(void) { TIM7->DIER &= ~TIM_DIER_UIE; }
void HAL_ResumeTick(void)  { TIM7->DIER |= TIM_DIER_UIE; }

/**
 * Hàm phục vụ ngắt (ISR) của TIM7 - chạy mỗi 1 ms.
 * Tên hàm phải đúng y như trong bảng vector (file startup) thì CPU mới gọi tới.
 */
void TIM7_IRQHandler(void)
{
    if (TIM7->SR & TIM_SR_UIF) {
        /* Phải xóa cờ ngắt, nếu không CPU sẽ vào lại ISR này liên tục.
         * Cờ trong SR xóa bằng cách ghi 0 vào bit đó (ghi 1 vào bit khác không ảnh hưởng). */
        TIM7->SR = ~TIM_SR_UIF;
        HAL_IncTick();   /* uwTick++ -> HAL_GetTick() tăng 1 */
    }
}
