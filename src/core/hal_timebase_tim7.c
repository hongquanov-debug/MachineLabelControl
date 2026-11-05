/**
 * @file hal_timebase_tim7.c
 * @brief HAL time base on TIM7 (1 kHz). SysTick is owned by FreeRTOS.
 *        Overrides the weak HAL_InitTick/HAL_SuspendTick/HAL_ResumeTick.
 */
#include "stm32f4xx_hal.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    __HAL_RCC_TIM7_CLK_ENABLE();

    /* TIM7 on APB1: timer clock = 2 x PCLK1 when APB1 prescaler != 1 */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_HCLK_DIV1) ? pclk1 : 2U * pclk1;

    TIM7->CR1  = 0;
    TIM7->PSC  = (timclk / 1000000U) - 1U;   /* 1 MHz */
    TIM7->ARR  = (1000000U / (1000U / (uint32_t)uwTickFreq)) - 1U;
    TIM7->EGR  = TIM_EGR_UG;
    TIM7->SR   = 0;
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1  = TIM_CR1_CEN;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }
    HAL_NVIC_SetPriority(TIM7_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    uwTickPrio = TickPriority;
    return HAL_OK;
}

void HAL_SuspendTick(void) { TIM7->DIER &= ~TIM_DIER_UIE; }
void HAL_ResumeTick(void)  { TIM7->DIER |= TIM_DIER_UIE; }

void TIM7_IRQHandler(void)
{
    if (TIM7->SR & TIM_SR_UIF) {
        TIM7->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}
