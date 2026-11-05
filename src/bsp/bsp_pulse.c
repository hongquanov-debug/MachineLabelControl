#include "bsp_pulse.h"

#include "app_config.h"
#include "board_pins.h"
#include "crit.h"

typedef struct {
    TIM_TypeDef  *tim;
    bool          apb2;
    IRQn_Type     irq;
    GPIO_TypeDef *pul_port;
    uint16_t      pul_pin;
    uint8_t       af;
    GPIO_TypeDef *dir_port;
    uint16_t      dir_pin;
} AxisHw_t;

typedef struct {
    RampState_t      ramp;
    volatile int32_t pos;
    int8_t           dir;
    volatile bool    busy;
} AxisRt_t;

static const AxisHw_t s_hw[AXIS_COUNT] = {
    [AXIS_M2] = {TIM1, true,  TIM1_UP_TIM10_IRQn, M2_PUL_PORT, M2_PUL_PIN, GPIO_AF1_TIM1, M2_DIR_PORT, M2_DIR_PIN},
    [AXIS_M3] = {TIM8, true,  TIM8_UP_TIM13_IRQn, M3_PUL_PORT, M3_PUL_PIN, GPIO_AF3_TIM8, M3_DIR_PORT, M3_DIR_PIN},
    [AXIS_M4] = {TIM3, false, TIM3_IRQn,          M4_PUL_PORT, M4_PUL_PIN, GPIO_AF2_TIM3, M4_DIR_PORT, M4_DIR_PIN},
    [AXIS_M5] = {TIM4, false, TIM4_IRQn,          M5_PUL_PORT, M5_PUL_PIN, GPIO_AF2_TIM4, M5_DIR_PORT, M5_DIR_PIN},
    [AXIS_M6] = {TIM9, true,  TIM1_BRK_TIM9_IRQn, M6_PUL_PORT, M6_PUL_PIN, GPIO_AF3_TIM9, M6_DIR_PORT, M6_DIR_PIN},
};

static AxisRt_t s_rt[AXIS_COUNT];

#define OCM_MASK           (TIM_CCMR1_OC1M)
#define OCM_FORCE_INACTIVE (TIM_CCMR1_OC1M_2)
#define OCM_PWM2           (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0)

static inline uint32_t freq_to_arr(float f)
{
    if (f < 200.0f) {
        f = 200.0f;      /* 10.5 MHz / 65536 = 160 Hz minimum */
    }
    uint32_t arr = (uint32_t)((float)PULSE_TIMER_TICK_HZ / f) - 1U;
    if (arr < 20U) {
        arr = 20U;       /* max ~500 kHz */
    } else if (arr > 0xFFFFU) {
        arr = 0xFFFFU;
    }
    return arr;
}

static inline void set_ocmode(TIM_TypeDef *t, uint32_t mode)
{
    t->CCMR1 = (t->CCMR1 & ~OCM_MASK) | mode;
}

static void hw_stop(Axis_t ax)
{
    TIM_TypeDef *t = s_hw[ax].tim;
    set_ocmode(t, OCM_FORCE_INACTIVE);   /* output low immediately */
    t->CR1 &= ~TIM_CR1_CEN;
    t->DIER &= ~TIM_DIER_UIE;
    t->SR = 0;
    s_rt[ax].busy = false;
}

void bsp_pulse_init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM9_CLK_ENABLE();

    for (int i = 0; i < (int)AXIS_COUNT; i++) {
        const AxisHw_t *hw = &s_hw[i];
        GPIO_InitTypeDef g = {0};

        HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, GPIO_PIN_RESET);
        g.Pin   = hw->dir_pin;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(hw->dir_port, &g);

        g.Pin       = hw->pul_pin;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = hw->af;
        HAL_GPIO_Init(hw->pul_port, &g);

        /* timer clock: APB2 timers 168 MHz, APB1 timers 84 MHz */
        uint32_t timclk = hw->apb2 ? 2U * HAL_RCC_GetPCLK2Freq() : 2U * HAL_RCC_GetPCLK1Freq();
        TIM_TypeDef *t = hw->tim;
        t->CR1   = TIM_CR1_ARPE;
        t->PSC   = (timclk / PULSE_TIMER_TICK_HZ) - 1U;
        t->ARR   = 0xFFFF;
        t->CCR1  = 0x7FFF;
        t->CCMR1 = OCM_FORCE_INACTIVE | TIM_CCMR1_OC1PE;
        t->CCER  = TIM_CCER_CC1E;           /* active high */
        if (IS_TIM_BREAK_INSTANCE(t)) {
            t->RCR  = 0;
            t->BDTR = TIM_BDTR_MOE;          /* TIM1/TIM8 main output enable */
        }
        t->EGR = TIM_EGR_UG;
        t->SR  = 0;

        HAL_NVIC_SetPriority(hw->irq, 2, 0);
        HAL_NVIC_EnableIRQ(hw->irq);
        s_rt[i].busy = false;
        s_rt[i].pos  = 0;
    }
}

bool bsp_pulse_start(Axis_t ax, bool forward, uint32_t pulses, const PulseProfile_t *prof)
{
    if ((unsigned)ax >= AXIS_COUNT || s_rt[ax].busy) {
        return false;
    }
    const AxisHw_t *hw = &s_hw[ax];
    AxisRt_t *rt = &s_rt[ax];
    TIM_TypeDef *t = hw->tim;

    HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, forward ? GPIO_PIN_SET : GPIO_PIN_RESET);
    rt->dir = forward ? 1 : -1;
    float f0 = ramp_init(&rt->ramp, prof, pulses);
    uint32_t arr = freq_to_arr(f0);

    /* NOTE: DIR setup time before first edge (>= 5 us for most drivers) is
     * guaranteed by PWM mode 2: the first half period is low. */
    t->CNT  = 0;
    t->ARR  = arr;
    t->CCR1 = (arr + 1U) / 2U;
    t->EGR  = TIM_EGR_UG;           /* load preload registers */
    t->SR   = 0;
    set_ocmode(t, OCM_PWM2);
    rt->busy = true;
    t->DIER |= TIM_DIER_UIE;
    t->CR1  |= TIM_CR1_CEN;
    return true;
}

void bsp_pulse_stop(Axis_t ax)
{
    uint32_t s = crit_enter();
    if (s_rt[ax].busy) {
        ramp_request_stop(&s_rt[ax].ramp);
    }
    crit_exit(s);
}

void bsp_pulse_abort(Axis_t ax)
{
    hw_stop(ax);
}

void bsp_pulse_finish_after(Axis_t ax, uint32_t extra)
{
    uint32_t s = crit_enter();
    if (s_rt[ax].busy) {
        ramp_finish_after(&s_rt[ax].ramp, extra);
    }
    crit_exit(s);
}

bool bsp_pulse_busy(Axis_t ax)        { return s_rt[ax].busy; }
int32_t bsp_pulse_position(Axis_t ax) { return s_rt[ax].pos; }

void bsp_pulse_set_position(Axis_t ax, int32_t pos)
{
    s_rt[ax].pos = pos;
}

/* Update event = one complete pulse period emitted */
void bsp_pulse_irq(Axis_t ax)
{
    TIM_TypeDef *t = s_hw[ax].tim;
    if ((t->SR & TIM_SR_UIF) == 0U) {
        return;
    }
    t->SR = ~TIM_SR_UIF;

    AxisRt_t *rt = &s_rt[ax];
    if (!rt->busy) {
        hw_stop(ax);
        return;
    }
    rt->pos += rt->dir;
    float f = ramp_step(&rt->ramp);
    if (f <= 0.0f) {
        hw_stop(ax);
        return;
    }
    uint32_t arr = freq_to_arr(f);
    t->ARR  = arr;                  /* preloaded: applies to next period */
    t->CCR1 = (arr + 1U) / 2U;
}
