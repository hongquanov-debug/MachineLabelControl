#include "bsp_board.h"

#include "app_config.h"

static void gpio_out(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState init)
{
    GPIO_InitTypeDef g = {0};
    HAL_GPIO_WritePin(port, pin, init);
    g.Pin   = pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &g);
}

static void gpio_in(GPIO_TypeDef *port, uint16_t pin, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = pull;
    HAL_GPIO_Init(port, &g);
}

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void bsp_delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

void bsp_board_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    dwt_init();

    /* Outputs - safe states first */
    gpio_out(KM1_PORT, KM1_PIN, GPIO_PIN_RESET);
    gpio_out(M6_BRAKE_PORT, M6_BRAKE_PIN, GPIO_PIN_RESET);     /* brake engaged */
    gpio_out(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    gpio_out(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_RESET);
    gpio_out(CAM_TRIG_PORT, CAM_TRIG_PIN, GPIO_PIN_RESET);

    /* Inputs */
    gpio_in(SAFE_ESTOP_PORT, SAFE_ESTOP_PIN, GPIO_PULLDOWN);   /* open wire = E-Stop */
    gpio_in(SAFE_DOOR_PORT, SAFE_DOOR_PIN, GPIO_PULLDOWN);
    gpio_in(SAFE_AIR_PORT, SAFE_AIR_PIN, GPIO_PULLDOWN);
    gpio_in(SAFE_RESET_PORT, SAFE_RESET_PIN, GPIO_PULLDOWN);
    gpio_in(S5_PORT, S5_PIN, GPIO_PULLUP);
    gpio_in(HOME_M3_PORT, HOME_M3_PIN, GPIO_PULLUP);
    gpio_in(M3_Z_PORT, M3_Z_PIN, GPIO_PULLUP);
    gpio_in(HOME_M4_PORT, HOME_M4_PIN, GPIO_PULLUP);
    gpio_in(HOME_M5_PORT, HOME_M5_PIN, GPIO_PULLUP);
    gpio_in(HOME_M6A_PORT, HOME_M6A_PIN, GPIO_PULLUP);
    gpio_in(HOME_M6B_PORT, HOME_M6B_PIN, GPIO_PULLUP);

    /* S2 can sensor -> EXTI2 (priority 5: may call FreeRTOS FromISR API) */
    GPIO_InitTypeDef g = {0};
    g.Pin  = S2_PIN;
    g.Mode = S2_TRIGGER_FALLING_EDGE ? GPIO_MODE_IT_FALLING : GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(S2_PORT, &g);
    HAL_NVIC_SetPriority(S2_EXTI_IRQn, 5, 0);
    /* enabled by tracking_init() */
}

bool bsp_estop_ok(void)             { return HAL_GPIO_ReadPin(SAFE_ESTOP_PORT, SAFE_ESTOP_PIN) == GPIO_PIN_SET; }
bool bsp_door_closed(void)          { return HAL_GPIO_ReadPin(SAFE_DOOR_PORT, SAFE_DOOR_PIN) == GPIO_PIN_SET; }
bool bsp_air_ok(void)               { return HAL_GPIO_ReadPin(SAFE_AIR_PORT, SAFE_AIR_PIN) == GPIO_PIN_SET; }
bool bsp_safety_reset_pressed(void) { return HAL_GPIO_ReadPin(SAFE_RESET_PORT, SAFE_RESET_PIN) == GPIO_PIN_SET; }

void bsp_km1_set(bool on)             { HAL_GPIO_WritePin(KM1_PORT, KM1_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void bsp_m6_brake_release(bool rel)   { HAL_GPIO_WritePin(M6_BRAKE_PORT, M6_BRAKE_PIN, rel ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void bsp_buzzer_set(bool on)          { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void bsp_led_toggle(void)             { HAL_GPIO_TogglePin(LED_RUN_PORT, LED_RUN_PIN); }

bool bsp_home_active(HomeSensor_t s)
{
    static GPIO_TypeDef *const port[HOME_COUNT] = {HOME_M3_PORT, HOME_M4_PORT, HOME_M5_PORT, HOME_M6A_PORT, HOME_M6B_PORT};
    static const uint16_t pin[HOME_COUNT] = {HOME_M3_PIN, HOME_M4_PIN, HOME_M5_PIN, HOME_M6A_PIN, HOME_M6B_PIN};
    if ((unsigned)s >= HOME_COUNT) {
        return false;
    }
    return HAL_GPIO_ReadPin(port[s], pin[s]) == HOME_ACTIVE_LEVEL;
}

bool bsp_s5_label_present(void)
{
    return HAL_GPIO_ReadPin(S5_PORT, S5_PIN) == S5_ACTIVE_LEVEL;
}

void bsp_cam_trigger_pulse(void)
{
    CAM_TRIG_PORT->BSRR = CAM_TRIG_PIN;
    bsp_delay_us(CAM_TRIG_PULSE_US);
    CAM_TRIG_PORT->BSRR = (uint32_t)CAM_TRIG_PIN << 16U;
}
