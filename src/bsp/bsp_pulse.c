/**
 * @file bsp_pulse.c
 * @brief Phát xung PUL/DIR cho 5 trục động cơ bước / servo (Spec Mục 4).
 *
 * ---------------------------------------------------------------------------
 *  NGUYÊN LÝ
 * ---------------------------------------------------------------------------
 *  Driver động cơ bước/servo nhận 2 tín hiệu:
 *    PUL : mỗi xung = động cơ quay 1 bước. Tần số xung = tốc độ quay.
 *    DIR : chiều quay (1 = thuận, 0 = nghịch).
 *
 *  Dùng timer ở chế độ PWM để phần cứng tự tạo xung:
 *    - Tần số đếm của mọi timer được chia về 10.5 MHz (PULSE_TIMER_TICK_HZ).
 *    - ARR quyết định chu kỳ: tần số xung = 10.5 MHz / (ARR + 1).
 *    - CCR1 = ARR/2 -> xung vuông 50%.
 *    - Mỗi chu kỳ = 1 xung. Khi timer tràn (update event) -> ngắt -> đếm số
 *      xung đã phát, tính tốc độ cho xung kế tiếp (tăng/giảm tốc hình thang,
 *      xem lib/pmems_core/src/pulse_ramp.c) và nạp ARR mới.
 *
 *  "Preload" (thanh ghi bóng): bit ARPE/OC1PE bật -> giá trị ghi vào ARR/CCR1
 *  chỉ có hiệu lực từ CHU KỲ SAU. Nhờ vậy đổi tốc độ giữa chừng không làm
 *  méo xung đang phát.
 *
 *  PWM mode 2: ngõ ra = 0 khi CNT < CCR1, = 1 khi CNT >= CCR1. Nửa đầu mỗi
 *  chu kỳ ở mức thấp -> có sẵn thời gian chờ cho driver sau khi đổi DIR.
 *
 *  Ngắt các timer này ở mức ưu tiên 2 (rất cao) và KHÔNG gọi API FreeRTOS.
 *  Task chỉ đọc cờ busy để biết trục đã chạy xong chưa.
 *
 *    Trục  Timer     Chân  Bus (xung timer)
 *    M2    TIM1_CH1  PE9   APB2 168 MHz
 *    M3    TIM8_CH1  PC6   APB2 168 MHz
 *    M4    TIM3_CH1  PA6   APB1  84 MHz
 *    M5    TIM4_CH1  PD12  APB1  84 MHz
 *    M6    TIM9_CH1  PE5   APB2 168 MHz (một chân xung cho cả M6a + M6b)
 * ---------------------------------------------------------------------------
 */
#include "bsp_pulse.h"

#include "app_config.h"
#include "board_pins.h"
#include "crit.h"

/* Phần cứng cố định của mỗi trục */
typedef struct {
    TIM_TypeDef  *tim;
    bool          apb2;       /* true: timer trên APB2 (168 MHz), false: APB1 (84 MHz) */
    IRQn_Type     irq;
    GPIO_TypeDef *pul_port;
    uint16_t      pul_pin;
    uint8_t       af;
    GPIO_TypeDef *dir_port;
    uint16_t      dir_pin;
} AxisHw_t;

/* Trạng thái lúc chạy (sửa trong ngắt -> volatile) */
typedef struct {
    RampState_t      ramp;    /* trạng thái tăng/giảm tốc */
    volatile int32_t pos;     /* vị trí tuyệt đối tính bằng xung */
    int8_t           dir;     /* +1 / -1 */
    volatile bool    busy;    /* true khi đang phát xung */
} AxisRt_t;

static const AxisHw_t s_hw[AXIS_COUNT] = {
    [AXIS_M2] = {TIM1, true,  TIM1_UP_TIM10_IRQn, M2_PUL_PORT, M2_PUL_PIN, GPIO_AF1_TIM1, M2_DIR_PORT, M2_DIR_PIN},
    [AXIS_M3] = {TIM8, true,  TIM8_UP_TIM13_IRQn, M3_PUL_PORT, M3_PUL_PIN, GPIO_AF3_TIM8, M3_DIR_PORT, M3_DIR_PIN},
    [AXIS_M4] = {TIM3, false, TIM3_IRQn,          M4_PUL_PORT, M4_PUL_PIN, GPIO_AF2_TIM3, M4_DIR_PORT, M4_DIR_PIN},
    [AXIS_M5] = {TIM4, false, TIM4_IRQn,          M5_PUL_PORT, M5_PUL_PIN, GPIO_AF2_TIM4, M5_DIR_PORT, M5_DIR_PIN},
    [AXIS_M6] = {TIM9, true,  TIM1_BRK_TIM9_IRQn, M6_PUL_PORT, M6_PUL_PIN, GPIO_AF3_TIM9, M6_DIR_PORT, M6_DIR_PIN},
};

static AxisRt_t s_rt[AXIS_COUNT];

/* Trường OC1M (bit 6:4 của CCMR1) chọn chế độ ngõ ra của kênh 1:
 *   100 = ép mức thấp (dừng xung ngay lập tức)
 *   111 = PWM mode 2                                              */
#define OCM_MASK           (TIM_CCMR1_OC1M)
#define OCM_FORCE_INACTIVE (TIM_CCMR1_OC1M_2)
#define OCM_PWM2           (TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0)

/** Đổi tần số (Hz) thành giá trị ARR, kẹp trong giới hạn của timer 16 bit. */
static inline uint32_t freq_to_arr(float f)
{
    if (f < 200.0f) {
        f = 200.0f;      /* 10.5 MHz / 65536 = 160 Hz là tần số thấp nhất đạt được */
    }
    uint32_t arr = (uint32_t)((float)PULSE_TIMER_TICK_HZ / f) - 1U;
    if (arr < 20U) {
        arr = 20U;       /* tối đa ~500 kHz */
    } else if (arr > 0xFFFFU) {
        arr = 0xFFFFU;
    }
    return arr;
}

/** Đổi chế độ ngõ ra kênh 1 mà không động tới các bit khác của CCMR1. */
static inline void set_ocmode(TIM_TypeDef *t, uint32_t mode)
{
    t->CCMR1 = (t->CCMR1 & ~OCM_MASK) | mode;
}

/** Dừng phát xung ngay lập tức. */
static void hw_stop(Axis_t ax)
{
    TIM_TypeDef *t = s_hw[ax].tim;
    set_ocmode(t, OCM_FORCE_INACTIVE);   /* ép chân xuống 0 trước, tránh xung thừa */
    t->CR1 &= ~TIM_CR1_CEN;              /* dừng bộ đếm */
    t->DIER &= ~TIM_DIER_UIE;            /* tắt ngắt */
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

        /* Chân DIR: GPIO output thường */
        HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, GPIO_PIN_RESET);
        g.Pin   = hw->dir_pin;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(hw->dir_port, &g);

        /* Chân PUL: Alternate Function -> nối vào kênh 1 của timer */
        g.Pin       = hw->pul_pin;
        g.Mode      = GPIO_MODE_AF_PP;
        g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;   /* xung tới 150 kHz cần sườn dốc */
        g.Alternate = hw->af;
        HAL_GPIO_Init(hw->pul_port, &g);

        /* Xung vào timer: APB2 = 168 MHz, APB1 = 84 MHz (xem system_clock.c) */
        uint32_t timclk = hw->apb2 ? 2U * HAL_RCC_GetPCLK2Freq() : 2U * HAL_RCC_GetPCLK1Freq();
        TIM_TypeDef *t = hw->tim;
        t->CR1   = TIM_CR1_ARPE;                          /* bật preload cho ARR */
        t->PSC   = (timclk / PULSE_TIMER_TICK_HZ) - 1U;   /* 168/16 hoặc 84/8 -> 10.5 MHz */
        t->ARR   = 0xFFFF;
        t->CCR1  = 0x7FFF;
        t->CCMR1 = OCM_FORCE_INACTIVE | TIM_CCMR1_OC1PE;  /* ngõ ra đang tắt, bật preload CCR1 */
        t->CCER  = TIM_CCER_CC1E;           /* cho phép kênh 1 xuất ra chân, mức tích cực = cao */
        if (IS_TIM_BREAK_INSTANCE(t)) {
            /* TIM1/TIM8 là "advanced timer" (dùng cho điều khiển động cơ 3 pha):
             * phải bật thêm bit MOE (Main Output Enable) thì chân mới có xung.
             * Đây là lỗi kinh điển: cấu hình đúng hết mà TIM1 không ra xung. */
            t->RCR  = 0;                     /* ngắt update sau MỖI chu kỳ */
            t->BDTR = TIM_BDTR_MOE;
        }
        t->EGR = TIM_EGR_UG;
        t->SR  = 0;

        /* Mức 2: cao hơn configMAX_SYSCALL (5) -> không bị FreeRTOS chặn,
         * nhưng vì vậy KHÔNG được gọi API FreeRTOS trong ngắt này. */
        HAL_NVIC_SetPriority(hw->irq, 2, 0);
        HAL_NVIC_EnableIRQ(hw->irq);
        s_rt[i].busy = false;
        s_rt[i].pos  = 0;
    }
}

/**
 * Bắt đầu phát @p pulses xung (0 = chạy liên tục tới khi gọi stop).
 * Hàm trả về ngay, phần cứng + ngắt tự lo việc phát xung.
 */
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
    float f0 = ramp_init(&rt->ramp, prof, pulses);   /* tần số xung đầu tiên (tốc độ khởi động) */
    uint32_t arr = freq_to_arr(f0);

    /* Driver cần DIR ổn định >= 5 us trước cạnh xung đầu tiên. PWM mode 2 có
     * nửa chu kỳ đầu ở mức thấp nên điều kiện này luôn được đảm bảo. */
    t->CNT  = 0;
    t->ARR  = arr;
    t->CCR1 = (arr + 1U) / 2U;
    t->EGR  = TIM_EGR_UG;           /* nạp ngay ARR/CCR1 từ thanh ghi preload */
    t->SR   = 0;
    set_ocmode(t, OCM_PWM2);
    rt->busy = true;
    t->DIER |= TIM_DIER_UIE;        /* ngắt mỗi lần tràn = mỗi xung */
    t->CR1  |= TIM_CR1_CEN;         /* chạy */
    return true;
}

/** Dừng có giảm tốc (an toàn cho cơ khí). */
void bsp_pulse_stop(Axis_t ax)
{
    uint32_t s = crit_enter();
    if (s_rt[ax].busy) {
        ramp_request_stop(&s_rt[ax].ramp);
    }
    crit_exit(s);
}

/** Dừng ngay không giảm tốc (E-Stop / lỗi). */
void bsp_pulse_abort(Axis_t ax)
{
    hw_stop(ax);
}

/** Đổi lệnh đang chạy thành "dừng sau @p extra xung nữa" (vd: gặp cảm biến + chạy thêm). */
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

/**
 * Ngắt update của timer trục @p ax = vừa phát xong 1 xung.
 * Chạy tới 150 000 lần/giây (trục M3) nên phải thật gọn.
 */
void bsp_pulse_irq(Axis_t ax)
{
    TIM_TypeDef *t = s_hw[ax].tim;
    if ((t->SR & TIM_SR_UIF) == 0U) {
        return;   /* vector dùng chung (vd TIM1_UP_TIM10): không phải ngắt của ta */
    }
    t->SR = ~TIM_SR_UIF;

    AxisRt_t *rt = &s_rt[ax];
    if (!rt->busy) {
        hw_stop(ax);
        return;
    }
    rt->pos += rt->dir;                 /* cập nhật vị trí */
    float f = ramp_step(&rt->ramp);     /* tần số cho xung kế tiếp, 0 = đã đủ số xung */
    if (f <= 0.0f) {
        hw_stop(ax);
        return;
    }
    uint32_t arr = freq_to_arr(f);
    t->ARR  = arr;                  /* có preload: áp dụng từ chu kỳ sau */
    t->CCR1 = (arr + 1U) / 2U;
}
