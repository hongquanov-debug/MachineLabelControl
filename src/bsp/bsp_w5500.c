#include "bsp_w5500.h"

#include "FreeRTOS.h"
#include "board_pins.h"
#include "task.h"

/* Common register block */
#define REG_MR       0x0000U
#define REG_GAR      0x0001U
#define REG_SUBR     0x0005U
#define REG_SHAR     0x0009U
#define REG_SIPR     0x000FU
#define REG_PHYCFGR  0x002EU
#define REG_VERSIONR 0x0039U

/* Socket register offsets */
#define Sn_MR        0x0000U
#define Sn_CR        0x0001U
#define Sn_SR        0x0003U
#define Sn_PORT      0x0004U
#define Sn_TX_FSR    0x0020U
#define Sn_TX_WR     0x0024U
#define Sn_RX_RSR    0x0026U
#define Sn_RX_RD     0x0028U

#define CR_OPEN      0x01U
#define CR_LISTEN    0x02U
#define CR_DISCON    0x08U
#define CR_CLOSE     0x10U
#define CR_SEND      0x20U
#define CR_RECV      0x40U

#define BSB_COMMON   0x00U
#define BSB_SREG(n)  ((uint8_t)((n) * 4U + 1U))
#define BSB_STX(n)   ((uint8_t)((n) * 4U + 2U))
#define BSB_SRX(n)   ((uint8_t)((n) * 4U + 3U))

static SPI_HandleTypeDef s_spi;

static inline void cs_low(void)  { W5500_CS_PORT->BSRR = (uint32_t)W5500_CS_PIN << 16U; }
static inline void cs_high(void) { W5500_CS_PORT->BSRR = W5500_CS_PIN; }

static void xfer(uint16_t addr, uint8_t bsb, bool write, uint8_t *data, uint16_t len)
{
    uint8_t hdr[3] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)((bsb << 3) | (write ? 0x04U : 0x00U))};
    cs_low();
    HAL_SPI_Transmit(&s_spi, hdr, 3, 10);
    if (write) {
        HAL_SPI_Transmit(&s_spi, data, len, 10U + len / 8U);
    } else {
        HAL_SPI_Receive(&s_spi, data, len, 10U + len / 8U);
    }
    cs_high();
}

static uint8_t rd8(uint16_t a, uint8_t bsb)          { uint8_t v; xfer(a, bsb, false, &v, 1); return v; }
static void    wr8(uint16_t a, uint8_t bsb, uint8_t v) { xfer(a, bsb, true, &v, 1); }
static uint16_t rd16(uint16_t a, uint8_t bsb)
{
    uint8_t b[2];
    xfer(a, bsb, false, b, 2);
    return (uint16_t)((b[0] << 8) | b[1]);
}
static void wr16(uint16_t a, uint8_t bsb, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    xfer(a, bsb, true, b, 2);
}
/* 16-bit counters must be read until two consecutive reads match (datasheet) */
static uint16_t rd16_stable(uint16_t a, uint8_t bsb)
{
    uint16_t v1, v2 = rd16(a, bsb);
    do {
        v1 = v2;
        v2 = rd16(a, bsb);
    } while (v1 != v2);
    return v1;
}

static void sock_cmd(uint8_t sn, uint8_t cmd)
{
    wr8(Sn_CR, BSB_SREG(sn), cmd);
    for (int i = 0; i < 100 && rd8(Sn_CR, BSB_SREG(sn)) != 0U; i++) {
    }
}

bool bsp_w5500_init(const W5500NetCfg_t *cfg)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = W5500_SCK_PIN | W5500_MISO_PIN | W5500_MOSI_PIN;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(W5500_SCK_PORT, &g);

    GPIO_InitTypeDef o = {0};
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_RESET);
    o.Pin   = W5500_CS_PIN;
    o.Mode  = GPIO_MODE_OUTPUT_PP;
    o.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(W5500_CS_PORT, &o);
    o.Pin = W5500_RST_PIN;
    HAL_GPIO_Init(W5500_RST_PORT, &o);
    o.Pin  = W5500_INT_PIN;
    o.Mode = GPIO_MODE_INPUT;
    o.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(W5500_INT_PORT, &o);

    s_spi.Instance               = SPI2;
    s_spi.Init.Mode              = SPI_MODE_MASTER;
    s_spi.Init.Direction         = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity       = SPI_POLARITY_LOW;
    s_spi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    s_spi.Init.NSS               = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;  /* 42/4 = 10.5 MHz */
    s_spi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode            = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&s_spi) != HAL_OK) {
        return false;
    }

    /* Hardware reset: RSTn low >= 500 us, then wait for PLL lock */
    vTaskDelay(pdMS_TO_TICKS(2));
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (rd8(REG_VERSIONR, BSB_COMMON) != 0x04U) {
        return false;
    }
    xfer(REG_SHAR, BSB_COMMON, true, (uint8_t *)(uintptr_t)cfg->mac, 6);
    xfer(REG_GAR, BSB_COMMON, true, (uint8_t *)(uintptr_t)cfg->gw, 4);
    xfer(REG_SUBR, BSB_COMMON, true, (uint8_t *)(uintptr_t)cfg->mask, 4);
    xfer(REG_SIPR, BSB_COMMON, true, (uint8_t *)(uintptr_t)cfg->ip, 4);
    return true;
}

bool bsp_w5500_link_up(void)
{
    return (rd8(REG_PHYCFGR, BSB_COMMON) & 0x01U) != 0U;
}

uint8_t bsp_w5500_sock_status(uint8_t sn)
{
    return rd8(Sn_SR, BSB_SREG(sn));
}

bool bsp_w5500_sock_listen(uint8_t sn, uint16_t port)
{
    sock_cmd(sn, CR_CLOSE);
    wr8(Sn_MR, BSB_SREG(sn), 0x01U);          /* TCP */
    wr16(Sn_PORT, BSB_SREG(sn), port);
    sock_cmd(sn, CR_OPEN);
    if (bsp_w5500_sock_status(sn) != W5500_SOCK_INIT) {
        return false;
    }
    sock_cmd(sn, CR_LISTEN);
    return bsp_w5500_sock_status(sn) == W5500_SOCK_LISTEN;
}

void bsp_w5500_sock_disconnect(uint8_t sn) { sock_cmd(sn, CR_DISCON); }
void bsp_w5500_sock_close(uint8_t sn)      { sock_cmd(sn, CR_CLOSE); }

uint16_t bsp_w5500_sock_rx_size(uint8_t sn)
{
    return rd16_stable(Sn_RX_RSR, BSB_SREG(sn));
}

uint16_t bsp_w5500_sock_recv(uint8_t sn, uint8_t *buf, uint16_t max)
{
    uint16_t len = bsp_w5500_sock_rx_size(sn);
    if (len == 0U) {
        return 0;
    }
    if (len > max) {
        len = max;
    }
    uint16_t ptr = rd16(Sn_RX_RD, BSB_SREG(sn));
    xfer(ptr, BSB_SRX(sn), false, buf, len);   /* chip wraps address in buffer */
    wr16(Sn_RX_RD, BSB_SREG(sn), (uint16_t)(ptr + len));
    sock_cmd(sn, CR_RECV);
    return len;
}

bool bsp_w5500_sock_send(uint8_t sn, const uint8_t *buf, uint16_t len)
{
    if (rd16_stable(Sn_TX_FSR, BSB_SREG(sn)) < len) {
        return false;
    }
    uint16_t ptr = rd16(Sn_TX_WR, BSB_SREG(sn));
    xfer(ptr, BSB_STX(sn), true, (uint8_t *)(uintptr_t)buf, len);
    wr16(Sn_TX_WR, BSB_SREG(sn), (uint16_t)(ptr + len));
    sock_cmd(sn, CR_SEND);
    return true;
}
