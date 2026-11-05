#include "bsp_rs485.h"

#include <string.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "board_pins.h"
#include "bsp_board.h"
#include "error_handler.h"
#include "semphr.h"

#define RX_BUF_SIZE 256U
#define TX_BUF_SIZE 260U

typedef struct {
    /* static configuration */
    USART_TypeDef       *instance;
    DMA_Stream_TypeDef  *dma_rx_stream;
    DMA_Stream_TypeDef  *dma_tx_stream;
    uint32_t             dma_channel;
    IRQn_Type            irq_uart, irq_dma_rx, irq_dma_tx;
    uint8_t              af;
    GPIO_TypeDef        *tx_port, *rx_port, *dir_port;
    uint16_t             tx_pin, rx_pin, dir_pin;
} PortHw_t;

typedef struct {
    UART_HandleTypeDef huart;
    DMA_HandleTypeDef  hdma_rx;
    DMA_HandleTypeDef  hdma_tx;
    SemaphoreHandle_t  tx_done;
    rs485_rx_cb_t      cb;
    void              *ctx;
    uint16_t           rx_pos;
    bool               tx_error;
    uint8_t            rx_buf[RX_BUF_SIZE];
    uint8_t            tx_buf[TX_BUF_SIZE];
} PortRt_t;

static const PortHw_t s_hw[RS485_PORT_COUNT] = {
    [RS485_HMI] = {USART2, DMA1_Stream5, DMA1_Stream6, DMA_CHANNEL_4,
                   USART2_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, GPIO_AF7_USART2,
                   RS485_1_TX_PORT, RS485_1_RX_PORT, RS485_1_DIR_PORT,
                   RS485_1_TX_PIN, RS485_1_RX_PIN, RS485_1_DIR_PIN},
    [RS485_VFD] = {USART3, DMA1_Stream1, DMA1_Stream3, DMA_CHANNEL_4,
                   USART3_IRQn, DMA1_Stream1_IRQn, DMA1_Stream3_IRQn, GPIO_AF7_USART3,
                   RS485_2_TX_PORT, RS485_2_RX_PORT, RS485_2_DIR_PORT,
                   RS485_2_TX_PIN, RS485_2_RX_PIN, RS485_2_DIR_PIN},
    [RS485_VISION] = {UART4, DMA1_Stream2, DMA1_Stream4, DMA_CHANNEL_4,
                      UART4_IRQn, DMA1_Stream2_IRQn, DMA1_Stream4_IRQn, GPIO_AF8_UART4,
                      RS485_3_TX_PORT, RS485_3_RX_PORT, RS485_3_DIR_PORT,
                      RS485_3_TX_PIN, RS485_3_RX_PIN, RS485_3_DIR_PIN},
};

static PortRt_t s_rt[RS485_PORT_COUNT];

static inline void dir_tx(Rs485Port_t p) { s_hw[p].dir_port->BSRR = s_hw[p].dir_pin; }
static inline void dir_rx(Rs485Port_t p) { s_hw[p].dir_port->BSRR = (uint32_t)s_hw[p].dir_pin << 16U; }

static int port_of(const UART_HandleTypeDef *h)
{
    for (int i = 0; i < (int)RS485_PORT_COUNT; i++) {
        if (h == &s_rt[i].huart) {
            return i;
        }
    }
    return -1;
}

static void start_rx(Rs485Port_t p)
{
    PortRt_t *rt = &s_rt[p];
    rt->rx_pos = 0;
    if (HAL_UARTEx_ReceiveToIdle_DMA(&rt->huart, rt->rx_buf, RX_BUF_SIZE) != HAL_OK) {
        return;
    }
    /* HT events are not needed: data is consumed on IDLE / TC */
    __HAL_DMA_DISABLE_IT(&rt->hdma_rx, DMA_IT_HT);
}

void bsp_rs485_init(Rs485Port_t p, uint32_t baud, Rs485Parity_t parity, rs485_rx_cb_t cb, void *ctx)
{
    const PortHw_t *hw = &s_hw[p];
    PortRt_t *rt = &s_rt[p];
    memset(rt, 0, sizeof(*rt));
    rt->cb  = cb;
    rt->ctx = ctx;

    __HAL_RCC_DMA1_CLK_ENABLE();
    switch (p) {
    case RS485_HMI:    __HAL_RCC_USART2_CLK_ENABLE(); break;
    case RS485_VFD:    __HAL_RCC_USART3_CLK_ENABLE(); break;
    case RS485_VISION: __HAL_RCC_UART4_CLK_ENABLE();  break;
    default: return;
    }

    /* GPIO */
    GPIO_InitTypeDef g = {0};
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = hw->af;
    g.Pin       = hw->tx_pin;
    HAL_GPIO_Init(hw->tx_port, &g);
    g.Pin = hw->rx_pin;
    HAL_GPIO_Init(hw->rx_port, &g);

    GPIO_InitTypeDef d = {0};
    HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, GPIO_PIN_RESET);
    d.Pin   = hw->dir_pin;
    d.Mode  = GPIO_MODE_OUTPUT_PP;
    d.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(hw->dir_port, &d);

    /* DMA RX (circular) */
    rt->hdma_rx.Instance                 = hw->dma_rx_stream;
    rt->hdma_rx.Init.Channel             = hw->dma_channel;
    rt->hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    rt->hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    rt->hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    rt->hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rt->hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    rt->hdma_rx.Init.Mode                = DMA_CIRCULAR;
    rt->hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    rt->hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&rt->hdma_rx) != HAL_OK) {
        error_handler();
    }
    __HAL_LINKDMA(&rt->huart, hdmarx, rt->hdma_rx);

    /* DMA TX (normal) */
    rt->hdma_tx.Instance                 = hw->dma_tx_stream;
    rt->hdma_tx.Init.Channel             = hw->dma_channel;
    rt->hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    rt->hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    rt->hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
    rt->hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rt->hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    rt->hdma_tx.Init.Mode                = DMA_NORMAL;
    rt->hdma_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    rt->hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&rt->hdma_tx) != HAL_OK) {
        error_handler();
    }
    __HAL_LINKDMA(&rt->huart, hdmatx, rt->hdma_tx);

    /* UART: 8 data bits; with parity the HAL word length must be 9 bits */
    rt->huart.Instance          = hw->instance;
    rt->huart.Init.BaudRate     = baud;
    rt->huart.Init.WordLength   = (parity == RS485_PARITY_NONE) ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    rt->huart.Init.StopBits     = UART_STOPBITS_1;
    rt->huart.Init.Parity       = (parity == RS485_PARITY_EVEN) ? UART_PARITY_EVEN
                                : (parity == RS485_PARITY_ODD)  ? UART_PARITY_ODD
                                                                : UART_PARITY_NONE;
    rt->huart.Init.Mode         = UART_MODE_TX_RX;
    rt->huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    rt->huart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&rt->huart) != HAL_OK) {
        error_handler();
    }

    rt->tx_done = xSemaphoreCreateBinary();
    configASSERT(rt->tx_done != NULL);

    HAL_NVIC_SetPriority(hw->irq_dma_rx, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_dma_rx);
    HAL_NVIC_SetPriority(hw->irq_dma_tx, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_dma_tx);
    HAL_NVIC_SetPriority(hw->irq_uart, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_uart);

    start_rx(p);
}

bool bsp_rs485_send(Rs485Port_t p, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    PortRt_t *rt = &s_rt[p];
    if (len == 0U || len > TX_BUF_SIZE) {
        return false;
    }
    memcpy(rt->tx_buf, data, len);
    (void)xSemaphoreTake(rt->tx_done, 0); /* clear stale */
    rt->tx_error = false;

    dir_tx(p);
    bsp_delay_us(RS485_DIR_SETUP_US);
    if (HAL_UART_Transmit_DMA(&rt->huart, rt->tx_buf, len) != HAL_OK) {
        dir_rx(p);
        return false;
    }
    if (xSemaphoreTake(rt->tx_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        HAL_UART_AbortTransmit(&rt->huart);
        dir_rx(p);
        return false;
    }
    return !rt->tx_error;
}

/* ------------------------------------------------------------------------ */
/* HAL callbacks (ISR context)                                              */
/* ------------------------------------------------------------------------ */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    int p = port_of(huart);
    if (p < 0) {
        return;
    }
    PortRt_t *rt = &s_rt[p];
    bool idle = (HAL_UARTEx_GetRxEventType(huart) == HAL_UART_RXEVENT_IDLE);

    if (size != rt->rx_pos) {
        if (size > rt->rx_pos) {
            if (rt->cb) {
                rt->cb(&rt->rx_buf[rt->rx_pos], (uint16_t)(size - rt->rx_pos), idle, rt->ctx);
            }
        } else {
            /* wrapped */
            if (rt->cb) {
                rt->cb(&rt->rx_buf[rt->rx_pos], (uint16_t)(RX_BUF_SIZE - rt->rx_pos), false, rt->ctx);
                if (size > 0U) {
                    rt->cb(&rt->rx_buf[0], size, idle, rt->ctx);
                }
            }
        }
        rt->rx_pos = size;
    } else if (idle && rt->cb) {
        rt->cb(NULL, 0, true, rt->ctx);
    }
    if (rt->rx_pos >= RX_BUF_SIZE) {
        rt->rx_pos = 0;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    int p = port_of(huart);
    if (p < 0) {
        return;
    }
    /* TC flag is set: last stop bit is on the line. Hold DIR, then release. */
    bsp_delay_us(RS485_DIR_HOLD_US);
    dir_rx((Rs485Port_t)p);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_rt[p].tx_done, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    int p = port_of(huart);
    if (p < 0) {
        return;
    }
    PortRt_t *rt = &s_rt[p];
    if ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0U && huart->gState == HAL_UART_STATE_READY) {
        /* TX DMA failed: release DIR and wake the sender */
        rt->tx_error = true;
        dir_rx((Rs485Port_t)p);
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(rt->tx_done, &woken);
        portYIELD_FROM_ISR(woken);
    }
    /* Restart reception after framing/noise/overrun errors (HAL aborted RX DMA) */
    if (huart->RxState == HAL_UART_STATE_READY) {
        start_rx((Rs485Port_t)p);
    }
}

void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
    int p = port_of(huart);
    if (p >= 0) {
        start_rx((Rs485Port_t)p);
    }
}

/* IRQ dispatch */
void bsp_rs485_irq_uart(Rs485Port_t p)   { HAL_UART_IRQHandler(&s_rt[p].huart); }
void bsp_rs485_irq_dma_rx(Rs485Port_t p) { HAL_DMA_IRQHandler(&s_rt[p].hdma_rx); }
void bsp_rs485_irq_dma_tx(Rs485Port_t p) { HAL_DMA_IRQHandler(&s_rt[p].hdma_tx); }
