/**
 * @file bsp_dio.c
 * @brief 32 ngõ vào (Module_LI32, 4 x 74HCT165) và 24 ngõ ra (Module_LO24, 3 x 74HCT595).
 *
 *  STM32 không đủ chân cho 56 tín hiệu 24V, nên dùng IC thanh ghi dịch:
 *  chỉ cần 3-4 chân GPIO để đọc/ghi hàng chục tín hiệu, bằng cách truyền
 *  NỐI TIẾP từng bit một ("bit-bang" = dùng phần mềm bật/tắt chân GPIO).
 *
 *  74HCT165 (ĐỌC, vào song song -> ra nối tiếp):
 *     LOAD xuống 0 : chụp 8 ngõ vào của mỗi IC cùng một lúc
 *     LOAD lên 1   : chuyển sang chế độ dịch
 *     mỗi xung CLK : bit kế tiếp xuất hiện ở chân DATA
 *
 *  74HCT595 (GHI, vào nối tiếp -> ra song song):
 *     đặt SER = bit cần ghi, tạo xung SRCLK   (lặp 24 lần)
 *     tạo xung RCLK: 24 bit được "chốt" ra chân cùng một lúc
 *     /OE = 0      : cho phép các ngõ ra hoạt động
 *
 *  Vì sao ghi thanh ghi BSRR/IDR trực tiếp thay vì HAL_GPIO_WritePin()?
 *  -> Mỗi lần quét phải bật/tắt chân hàng trăm lần. Ghi thẳng thanh ghi nhanh
 *     hơn nhiều lần và vẫn an toàn (BSRR là phép ghi một bước).
 */
#include "bsp_dio.h"

#include "board_pins.h"
#include "crit.h"

/* "Ảnh" 24 ngõ ra trong RAM: bit n = trạng thái mong muốn của DO thứ n.
 * volatile: báo trình biên dịch biến này có thể bị thay đổi bởi ngắt, nên
 * phải đọc lại từ RAM mỗi lần (không được tối ưu giữ trong thanh ghi CPU). */
static volatile uint32_t s_do_image;
static bool s_oe_enabled;

/* Macro thao tác chân nhanh: PIN_HI = lên 1, PIN_LO = xuống 0, PIN_RD = đọc.
 * IDR (Input Data Register) chứa mức logic hiện tại của 16 chân trong port. */
#define PIN_HI(p, n) ((p)->BSRR = (n))
#define PIN_LO(p, n) ((p)->BSRR = (uint32_t)(n) << 16U)
#define PIN_RD(p, n) (((p)->IDR & (n)) != 0U)

/* IC 74HCT cần xung rộng tối thiểu ~20 ns. CPU 168 MHz bật/tắt chân nhanh
 * hơn thế, nên chèn vài lệnh NOP (mỗi NOP ~6 ns) để giữ >= 50 ns. */
static inline void short_delay(void)
{
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
}

/** Cấu hình chân output, đặt mức ban đầu trước để không bị giật mức. */
static void out_pin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState init)
{
    GPIO_InitTypeDef g = {0};
    HAL_GPIO_WritePin(port, pin, init);
    g.Pin   = pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &g);
}

void bsp_dio_init(uint32_t initial_do)
{
    /* LO24: giữ /OE = 1 (ngõ ra bị vô hiệu) cho tới lần chốt dữ liệu hợp lệ đầu tiên.
     * Nếu không, lúc mới cấp nguồn 74HC595 có thể xuất giá trị ngẫu nhiên
     * -> van khí nén hoặc driver động cơ có thể bị kích nhầm.               */
    out_pin(DO_OE_PORT, DO_OE_PIN, GPIO_PIN_SET);
    out_pin(DO_SER_PORT, DO_SER_PIN, GPIO_PIN_RESET);
    out_pin(DO_SRCLK_PORT, DO_SRCLK_PIN, GPIO_PIN_RESET);
    out_pin(DO_RCLK_PORT, DO_RCLK_PIN, GPIO_PIN_RESET);

    /* LI32 */
    out_pin(DI_LOAD_PORT, DI_LOAD_PIN, GPIO_PIN_SET);
    out_pin(DI_CLK_PORT, DI_CLK_PIN, GPIO_PIN_RESET);
    GPIO_InitTypeDef g = {0};
    g.Pin  = DI_DATA_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;   /* tuột cáp -> chân lơ lửng đọc ra 1 -> byte thứ 5 = 0xFF -> ALM_20 */
    HAL_GPIO_Init(DI_DATA_PORT, &g);

    s_do_image   = initial_do & ((1UL << DIO_DO_COUNT) - 1U);  /* chỉ giữ 24 bit thấp */
    s_oe_enabled = false;
    bsp_dio_do_commit();    /* xuất trạng thái an toàn rồi mới bật /OE */
}

void bsp_dio_scan(uint32_t *raw, uint8_t *extra)
{
    uint8_t bytes[5];

    /* Xung LOAD mức thấp: cả 4 IC chụp trạng thái 32 ngõ vào cùng một thời điểm */
    PIN_LO(DI_LOAD_PORT, DI_LOAD_PIN);
    short_delay();
    PIN_HI(DI_LOAD_PORT, DI_LOAD_PIN);
    short_delay();

    /* Đọc 5 byte = 40 bit. 4 byte đầu là 32 ngõ vào; byte thứ 5 là chân SER
     * của IC cuối chuỗi (nối GND trên module -> luôn đọc 0x00 khi cáp tốt). */
    for (unsigned b = 0; b < 5U; b++) {
        uint8_t v = 0;
        for (int bit = 7; bit >= 0; bit--) {   /* bit cao (D7/QH) ra trước */
            if (PIN_RD(DI_DATA_PORT, DI_DATA_PIN)) {
                v |= (uint8_t)(1U << bit);
            }
            PIN_HI(DI_CLK_PORT, DI_CLK_PIN);   /* cạnh lên CLK: dịch bit kế tiếp ra */
            short_delay();
            PIN_LO(DI_CLK_PORT, DI_CLK_PIN);
            short_delay();
        }
        bytes[b] = v;
    }
    /* Ghép 4 byte thành 1 số 32 bit: byte 0 ở bit 0..7, byte 1 ở bit 8..15, ...
     * => chỉ số bit = byte * 8 + bit, khớp với bảng Mục 5.1 của spec. */
    *raw = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    *extra = bytes[4];
}

/** Đổi 1 bit trong ảnh ngõ ra (chưa xuất ra chân). An toàn khi gọi từ ngắt. */
void bsp_dio_do_write(uint8_t index, bool on)
{
    if (index >= DIO_DO_COUNT) {
        return;
    }
    uint32_t s = crit_enter();   /* đọc-sửa-ghi phải liền mạch, xem crit.h */
    if (on) {
        s_do_image |= (1UL << index);    /* bật bit */
    } else {
        s_do_image &= ~(1UL << index);   /* xóa bit */
    }
    crit_exit(s);
}

bool bsp_dio_do_get(uint8_t index)
{
    return (index < DIO_DO_COUNT) && ((s_do_image >> index) & 1U);
}

uint32_t bsp_dio_do_image(void)
{
    return s_do_image;
}

/** Dịch 24 bit ra chuỗi 74HCT595 rồi chốt. Task I/O gọi mỗi 1 ms; các trạm cần
 *  thời gian chính xác (dán tem) gọi ngay qua do_commit_now(). */
void bsp_dio_do_commit(void)
{
    /* Cả quá trình dịch (~3 us) nằm trong vùng găng để 2 task không cùng dịch
     * xen kẽ làm sai dữ liệu. Ngắt phát xung (mức 2) vẫn không bị chặn. */
    uint32_t s = crit_enter();
    uint32_t img = s_do_image;
    /* Dịch bit 23 trước: bit dịch vào đầu tiên sẽ bị đẩy tới IC xa nhất (byte 2). */
    for (int i = (int)DIO_DO_COUNT - 1; i >= 0; i--) {
        if ((img >> i) & 1U) {
            PIN_HI(DO_SER_PORT, DO_SER_PIN);
        } else {
            PIN_LO(DO_SER_PORT, DO_SER_PIN);
        }
        short_delay();
        PIN_HI(DO_SRCLK_PORT, DO_SRCLK_PIN);   /* cạnh lên SRCLK: IC nhận bit */
        short_delay();
        PIN_LO(DO_SRCLK_PORT, DO_SRCLK_PIN);
    }
    PIN_HI(DO_RCLK_PORT, DO_RCLK_PIN);         /* cạnh lên RCLK: 24 ngõ ra đổi đồng thời */
    short_delay();
    PIN_LO(DO_RCLK_PORT, DO_RCLK_PIN);
    if (!s_oe_enabled) {
        PIN_LO(DO_OE_PORT, DO_OE_PIN);         /* lần đầu: cho phép ngõ ra */
        s_oe_enabled = true;
    }
    crit_exit(s);
}

void bsp_dio_do_all_off(void)
{
    uint32_t s = crit_enter();
    s_do_image = 0;
    crit_exit(s);
    bsp_dio_do_commit();
}
