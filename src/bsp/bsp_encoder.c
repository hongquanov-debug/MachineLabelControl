#include "bsp_encoder.h"

#include "board_pins.h"
#include "error_handler.h"

static TIM_HandleTypeDef htim2;

void bsp_encoder_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = ENC_A_PIN | ENC_B_PIN;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(ENC_A_PORT, &g);

    TIM_Encoder_InitTypeDef enc = {0};
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 0xFFFFFFFFU;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    enc.EncoderMode  = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity  = TIM_ICPOLARITY_RISING;   /* TODO(HW): swap to invert count direction */
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter    = 0x6;                      /* digital noise filter */
    enc.IC2Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter    = 0x6;
    if (HAL_TIM_Encoder_Init(&htim2, &enc) != HAL_OK) {
        error_handler();
    }
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
}
