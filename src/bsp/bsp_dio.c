#include "bsp_dio.h"

#include "board_pins.h"
#include "crit.h"

static volatile uint32_t s_do_image;
static bool s_oe_enabled;

#define PIN_HI(p, n) ((p)->BSRR = (n))
#define PIN_LO(p, n) ((p)->BSRR = (uint32_t)(n) << 16U)
#define PIN_RD(p, n) (((p)->IDR & (n)) != 0U)

/* 74HCT @ 3.3 V input: tW ~ 20 ns. A few NOPs keep >= 50 ns at 168 MHz. */
static inline void short_delay(void)
{
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
}

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
    /* LO24: keep /OE high (outputs disabled) until the first valid commit */
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
    g.Pull = GPIO_PULLUP;   /* floating cable -> reads 1 -> 0xFF detection */
    HAL_GPIO_Init(DI_DATA_PORT, &g);

    s_do_image   = initial_do & ((1UL << DIO_DO_COUNT) - 1U);
    s_oe_enabled = false;
    bsp_dio_do_commit();
}

void bsp_dio_scan(uint32_t *raw, uint8_t *extra)
{
    uint8_t bytes[5];

    /* Parallel load (SH/LD low pulse) */
    PIN_LO(DI_LOAD_PORT, DI_LOAD_PIN);
    short_delay();
    PIN_HI(DI_LOAD_PORT, DI_LOAD_PIN);
    short_delay();

    for (unsigned b = 0; b < 5U; b++) {
        uint8_t v = 0;
        for (int bit = 7; bit >= 0; bit--) {   /* QH (D7) comes out first */
            if (PIN_RD(DI_DATA_PORT, DI_DATA_PIN)) {
                v |= (uint8_t)(1U << bit);
            }
            PIN_HI(DI_CLK_PORT, DI_CLK_PIN);
            short_delay();
            PIN_LO(DI_CLK_PORT, DI_CLK_PIN);
            short_delay();
        }
        bytes[b] = v;
    }
    *raw = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    *extra = bytes[4];
}

void bsp_dio_do_write(uint8_t index, bool on)
{
    if (index >= DIO_DO_COUNT) {
        return;
    }
    uint32_t s = crit_enter();
    if (on) {
        s_do_image |= (1UL << index);
    } else {
        s_do_image &= ~(1UL << index);
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

void bsp_dio_do_commit(void)
{
    uint32_t s = crit_enter();
    uint32_t img = s_do_image;
    /* Byte 2 is shifted first so it ends in the farthest 595; MSB first. */
    for (int i = (int)DIO_DO_COUNT - 1; i >= 0; i--) {
        if ((img >> i) & 1U) {
            PIN_HI(DO_SER_PORT, DO_SER_PIN);
        } else {
            PIN_LO(DO_SER_PORT, DO_SER_PIN);
        }
        short_delay();
        PIN_HI(DO_SRCLK_PORT, DO_SRCLK_PIN);
        short_delay();
        PIN_LO(DO_SRCLK_PORT, DO_SRCLK_PIN);
    }
    PIN_HI(DO_RCLK_PORT, DO_RCLK_PIN);
    short_delay();
    PIN_LO(DO_RCLK_PORT, DO_RCLK_PIN);
    if (!s_oe_enabled) {
        PIN_LO(DO_OE_PORT, DO_OE_PIN);
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
