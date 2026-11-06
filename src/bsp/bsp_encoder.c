/**
 * @file bsp_encoder.c
 * @brief Đọc encoder băng tải bằng TIM2 ở chế độ Encoder (Spec Mục 1).
 *
 *  Encoder quang phát ra 2 tín hiệu vuông A và B lệch pha 90°. Timer của STM32
 *  có sẵn chế độ "Encoder mode": phần cứng tự đếm lên/xuống theo thứ tự cạnh
 *  của A và B, CPU không phải làm gì. Muốn biết vị trí chỉ cần đọc TIM2->CNT.
 *
 *  Chọn TIM2 vì nó là timer 32-bit: đếm tới 4.29 tỉ xung = 429 km băng tải
 *  mới tràn. Các phép tính khoảng cách dùng trừ số không dấu nên kể cả khi
 *  tràn vẫn đúng.
 *
 *  Đây là ví dụ dùng HAL "đầy đủ" với Handle (xem docs/HAL_GUIDE.md):
 *    TIM_HandleTypeDef  htim2 : "đối tượng" đại diện cho TIM2, HAL lưu trạng thái trong đó
 *    htim2.Instance           : trỏ tới khối thanh ghi TIM2
 *    htim2.Init               : cấu hình cơ bản của timer
 *    TIM_Encoder_InitTypeDef  : cấu hình riêng cho chế độ encoder
 */
#include "bsp_encoder.h"

#include "board_pins.h"
#include "error_handler.h"

/* Handle phải là biến static/toàn cục (không đặt trên stack của hàm) vì HAL
 * còn dùng nó sau khi hàm init kết thúc. */
static TIM_HandleTypeDef htim2;

void bsp_encoder_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Chân PA0/PA1 làm "Alternate Function": chân không còn là GPIO thường mà
     * được nối vào ngoại vi (ở đây là kênh 1, 2 của TIM2). Mỗi chân có bảng
     * AF0..AF15 trong datasheet; TIM2 trên PA0/PA1 là AF1.                  */
    GPIO_InitTypeDef g = {0};
    g.Pin       = ENC_A_PIN | ENC_B_PIN;     /* cấu hình 2 chân cùng lúc bằng phép OR */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(ENC_A_PORT, &g);

    TIM_Encoder_InitTypeDef enc = {0};
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;                  /* đếm mọi cạnh, không chia */
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 0xFFFFFFFFU;        /* đếm hết dải 32 bit */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    /* TI12 = đếm cả cạnh của A và B (x4): độ phân giải cao nhất. */
    enc.EncoderMode  = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity  = TIM_ICPOLARITY_RISING;   /* TODO(HW): đổi FALLING để đảo chiều đếm */
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter    = 0x6;                      /* lọc nhiễu số: bỏ các gai ngắn trên dây encoder */
    enc.IC2Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter    = 0x6;
    if (HAL_TIM_Encoder_Init(&htim2, &enc) != HAL_OK) {
        error_handler();
    }
    /* Init chỉ cấu hình; phải Start thì timer mới bắt đầu đếm. */
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
}
