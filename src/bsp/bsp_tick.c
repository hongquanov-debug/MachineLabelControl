#include "bsp_tick.h"

#include "stm32f4xx_hal.h"

static bsp_tick_cb_t s_cb;

void bsp_tick_init(uint32_t hz, bsp_tick_cb_t cb)
{
    s_cb = cb;
    __HAL_RCC_TIM6_CLK_ENABLE();

    uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV1) ? pclk1 : 2U * pclk1;

    TIM6->CR1  = 0;
    TIM6->PSC  = (timclk / 1000000U) - 1U;  /* 1 MHz */
    TIM6->ARR  = (1000000U / hz) - 1U;
    TIM6->EGR  = TIM_EGR_UG;
    TIM6->SR   = 0;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

void bsp_tick_irq(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;
        if (s_cb != NULL) {
            s_cb();
        }
    }
}
