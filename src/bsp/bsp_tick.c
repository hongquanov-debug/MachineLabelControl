/**
 * @file bsp_tick.c
 * @brief TIM6 tạo ngắt định kỳ 5 kHz (mỗi 0.2 ms) để quét theo dõi lon.
 *
 *  TIM6 là "basic timer": chỉ biết đếm và báo tràn, không có chân ra -> rất
 *  hợp để làm nhịp định kỳ. Ở 312 mm/s, 0.2 ms tương ứng lon đi 0.06 mm,
 *  đủ mịn so với sai số ±0.5 mm của trạm dán tem.
 *
 *  Kỹ thuật "callback bằng con trỏ hàm": module này không biết ai cần nhịp
 *  5 kHz. Người dùng truyền vào một hàm (cb), ISR sẽ gọi hàm đó. Nhờ vậy
 *  bsp (phần cứng) không phải include gì của app (logic).
 */
#include "bsp_tick.h"

#include "stm32f4xx_hal.h"

static bsp_tick_cb_t s_cb;   /* hàm sẽ được gọi mỗi lần ngắt (tracking_tick_isr) */

void bsp_tick_init(uint32_t hz, bsp_tick_cb_t cb)
{
    s_cb = cb;
    __HAL_RCC_TIM6_CLK_ENABLE();

    /* Tính xung vào timer (84 MHz), xem giải thích trong system_clock.c */
    uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV1) ? pclk1 : 2U * pclk1;

    /* Ghi thanh ghi trực tiếp, cùng cách với TIM7 trong hal_timebase_tim7.c */
    TIM6->CR1  = 0;
    TIM6->PSC  = (timclk / 1000000U) - 1U;  /* 84 MHz / 84 = 1 MHz */
    TIM6->ARR  = (1000000U / hz) - 1U;      /* 1 MHz / 5000 = 200 lần đếm = 0.2 ms */
    TIM6->EGR  = TIM_EGR_UG;
    TIM6->SR   = 0;
    TIM6->DIER = TIM_DIER_UIE;
    /* Mức 5: được phép gọi API FreeRTOS ...FromISR() (gửi queue cho các task trạm) */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;                /* bắt đầu đếm */
}

/** Gọi từ TIM6_DAC_IRQHandler (stm32f4xx_it.c). */
void bsp_tick_irq(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;   /* xóa cờ tràn */
        if (s_cb != NULL) {
            s_cb();
        }
    }
}
