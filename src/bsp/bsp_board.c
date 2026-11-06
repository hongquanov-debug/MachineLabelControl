/**
 * @file bsp_board.c
 * @brief GPIO cấp board: chuỗi an toàn, cảm biến home, trigger camera, delay micro-giây.
 *
 *  BSP = Board Support Package: lớp code "biết" phần cứng cụ thể của board
 *  (chân nào nối với cái gì). Lớp app phía trên chỉ gọi bsp_estop_ok(),
 *  bsp_km1_set()... mà không cần biết đó là chân PC0 hay PC4.
 *
 *  Các bước dùng một chân GPIO bằng HAL:
 *    1. Bật xung port:   __HAL_RCC_GPIOx_CLK_ENABLE()
 *    2. Cấu hình chân:   điền GPIO_InitTypeDef rồi gọi HAL_GPIO_Init()
 *    3. Đọc / ghi:       HAL_GPIO_ReadPin(), HAL_GPIO_WritePin(), HAL_GPIO_TogglePin()
 */
#include "bsp_board.h"

#include "app_config.h"

/** Cấu hình một chân thành NGÕ RA, đặt mức ban đầu TRƯỚC khi bật chế độ output
 *  để chân không bị "giật" sang mức sai trong khoảnh khắc chuyển chế độ. */
static void gpio_out(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState init)
{
    GPIO_InitTypeDef g = {0};
    HAL_GPIO_WritePin(port, pin, init);   /* nạp sẵn mức mong muốn vào thanh ghi ODR */
    g.Pin   = pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;        /* Push-Pull: chủ động kéo lên 3.3V hoặc xuống 0V */
    g.Pull  = GPIO_NOPULL;                /* ngõ ra không cần điện trở kéo */
    g.Speed = GPIO_SPEED_FREQ_HIGH;       /* tốc độ chuyển mạch của chân (ảnh hưởng nhiễu EMI) */
    HAL_GPIO_Init(port, &g);
}

/** Cấu hình một chân thành NGÕ VÀO với điện trở kéo nội (PULLUP / PULLDOWN / NOPULL). */
static void gpio_in(GPIO_TypeDef *port, uint16_t pin, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = pull;
    HAL_GPIO_Init(port, &g);
}

/* DWT (Data Watchpoint and Trace) là khối gỡ lỗi của lõi ARM, có bộ đếm
 * CYCCNT tăng 1 sau MỖI chu kỳ CPU (168 triệu lần/giây). Dùng nó để đo/chờ
 * thời gian rất ngắn (micro-giây) mà HAL_Delay() (đơn vị ms) không làm được. */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* bật khối trace */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             /* cho bộ đếm chạy */
}

/** Chờ bận (busy-wait) @p us micro-giây. Chỉ dùng cho khoảng rất ngắn (< 50 us)
 *  vì CPU bị chiếm hoàn toàn trong lúc chờ. Phép trừ unsigned vẫn đúng khi
 *  CYCCNT tràn qua 0xFFFFFFFF. */
void bsp_delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t ticks = us * (SystemCoreClock / 1000000U);   /* SystemCoreClock = 168000000 */
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

void bsp_board_init(void)
{
    /* Bước 1: bật xung cho tất cả các port GPIO dùng trên board */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    dwt_init();

    /* Bước 2: NGÕ RA - đặt trạng thái an toàn trước tiên */
    gpio_out(KM1_PORT, KM1_PIN, GPIO_PIN_RESET);              /* contactor KM1 tắt */
    gpio_out(M6_BRAKE_PORT, M6_BRAKE_PIN, GPIO_PIN_RESET);    /* phanh M6 đóng (mất điện = phanh) */
    gpio_out(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    gpio_out(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_RESET);
    gpio_out(CAM_TRIG_PORT, CAM_TRIG_PIN, GPIO_PIN_RESET);
    gpio_out(CAM2_TRIG_PORT, CAM2_TRIG_PIN, GPIO_PIN_RESET);

    /* Bước 3: NGÕ VÀO.
     * Tín hiệu an toàn dùng PULLDOWN: nếu dây bị đứt, chân tự về 0 = "không an
     * toàn" (E-Stop đang nhấn / cửa mở). Nguyên tắc "hỏng thì về phía an toàn". */
    gpio_in(SAFE_ESTOP_PORT, SAFE_ESTOP_PIN, GPIO_PULLDOWN);
    gpio_in(SAFE_DOOR_PORT, SAFE_DOOR_PIN, GPIO_PULLDOWN);
    gpio_in(SAFE_AIR_PORT, SAFE_AIR_PIN, GPIO_PULLDOWN);
    gpio_in(SAFE_RESET_PORT, SAFE_RESET_PIN, GPIO_PULLDOWN);
    /* Cảm biến NPN: khi tác động sẽ kéo chân xuống 0 -> dùng PULLUP */
    gpio_in(S5_PORT, S5_PIN, GPIO_PULLUP);
    gpio_in(HOME_M3_PORT, HOME_M3_PIN, GPIO_PULLUP);
    gpio_in(M3_Z_PORT, M3_Z_PIN, GPIO_PULLUP);
    gpio_in(HOME_M4_PORT, HOME_M4_PIN, GPIO_PULLUP);
    gpio_in(HOME_M5_PORT, HOME_M5_PIN, GPIO_PULLUP);
    gpio_in(HOME_M6A_PORT, HOME_M6A_PIN, GPIO_PULLUP);
    gpio_in(HOME_M6B_PORT, HOME_M6B_PIN, GPIO_PULLUP);

    /* Bước 4: chân cảm biến lon S2 dùng NGẮT NGOÀI (EXTI).
     * GPIO_MODE_IT_FALLING = sinh ngắt khi chân chuyển 1 -> 0. HAL_GPIO_Init()
     * tự cấu hình khối EXTI và SYSCFG (chọn port E cho line 2).
     * Mức ưu tiên 5 = configMAX_SYSCALL_INTERRUPT_PRIORITY: thấp nhất mà vẫn
     * được phép gọi API FreeRTOS dạng ...FromISR(). Ngắt CHƯA được bật ở đây
     * (bật trong tracking_init() khi dữ liệu theo dõi lon đã sẵn sàng).        */
    GPIO_InitTypeDef g = {0};
    g.Pin  = S2_PIN;
    g.Mode = S2_TRIGGER_FALLING_EDGE ? GPIO_MODE_IT_FALLING : GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(S2_PORT, &g);
    HAL_NVIC_SetPriority(S2_EXTI_IRQn, 5, 0);
    /* enabled by tracking_init() */
}

/* HAL_GPIO_ReadPin() trả về GPIO_PIN_SET (mức 1) hoặc GPIO_PIN_RESET (mức 0). */
bool bsp_estop_ok(void)             { return HAL_GPIO_ReadPin(SAFE_ESTOP_PORT, SAFE_ESTOP_PIN) == GPIO_PIN_SET; }
bool bsp_door_closed(void)          { return HAL_GPIO_ReadPin(SAFE_DOOR_PORT, SAFE_DOOR_PIN) == GPIO_PIN_SET; }
bool bsp_air_ok(void)               { return HAL_GPIO_ReadPin(SAFE_AIR_PORT, SAFE_AIR_PIN) == GPIO_PIN_SET; }
bool bsp_safety_reset_pressed(void) { return HAL_GPIO_ReadPin(SAFE_RESET_PORT, SAFE_RESET_PIN) == GPIO_PIN_SET; }

/* KM1 = contactor cấp nguồn động lực. Ở chế độ bàn thử (SAFETY_BENCH_MODE) chuỗi
 * an toàn bị bỏ qua, nên KM1 bị KHÓA TẮT tại đây: không bao giờ có nguồn động lực. */
void bsp_km1_set(bool on)
{
#if SAFETY_BENCH_MODE
    (void)on;
    HAL_GPIO_WritePin(KM1_PORT, KM1_PIN, GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(KM1_PORT, KM1_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}
void bsp_m6_brake_release(bool rel)   { HAL_GPIO_WritePin(M6_BRAKE_PORT, M6_BRAKE_PIN, rel ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void bsp_buzzer_set(bool on)          { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void bsp_led_toggle(void)             { HAL_GPIO_TogglePin(LED_RUN_PORT, LED_RUN_PIN); }

/** Đọc cảm biến home theo bảng tra (mảng port/pin) thay cho một chuỗi if/else. */
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

/** Xung 10 us kích camera + đèn strobe. Gọi từ NGẮT nên dùng BSRR trực tiếp
 *  (nhanh, an toàn) thay cho HAL_GPIO_WritePin(). */
void bsp_cam_trigger_pulse(void)
{
    CAM_TRIG_PORT->BSRR = CAM_TRIG_PIN;                    /* lên mức 1 */
    bsp_delay_us(CAM_TRIG_PULSE_US);
    CAM_TRIG_PORT->BSRR = (uint32_t)CAM_TRIG_PIN << 16U;   /* xuống mức 0 */
}

/** Xung 10 us kích camera 2 (kiểm tra mã DataMatrix sau khi dán). */
void bsp_cam2_trigger_pulse(void)
{
    CAM2_TRIG_PORT->BSRR = CAM2_TRIG_PIN;
    bsp_delay_us(CAM_TRIG_PULSE_US);
    CAM2_TRIG_PORT->BSRR = (uint32_t)CAM2_TRIG_PIN << 16U;
}
