/**
 * @file bsp_rs485.c
 * @brief 3 cổng RS485 bán song công: nhận bằng DMA vòng + phát hiện IDLE,
 *        gửi bằng DMA, tự điều khiển chân DIR (Spec Mục 2.1, Mục 7).
 *
 * ---------------------------------------------------------------------------
 *  KIẾN THỨC NỀN CHO NGƯỜI MỚI
 * ---------------------------------------------------------------------------
 *  UART: truyền nối tiếp từng byte. RS485: chuẩn điện cho UART đi xa (hàng
 *  trăm mét) trên 2 dây A/B. "Bán song công" = cùng một cặp dây nên tại một
 *  thời điểm chỉ một bên được nói. IC chuyển đổi (MAX485...) có chân DIR:
 *    DIR = 1 -> MCU đang phát,  DIR = 0 -> MCU đang nghe.
 *
 *  DMA (Direct Memory Access): bộ "chép dữ liệu" phần cứng. Khi nhận, mỗi
 *  byte UART về được DMA tự chép vào mảng rx_buf mà CPU không phải xử lý
 *  ngắt cho từng byte. Chế độ CIRCULAR: chép tới cuối mảng thì quay lại đầu.
 *
 *  IDLE line: UART báo khi đường truyền im lặng hơn 1 byte -> nghĩa là một
 *  gói tin vừa kết thúc. Nhờ vậy nhận được gói có độ dài bất kỳ mà không cần
 *  biết trước số byte.
 *
 *  Luồng xử lý của HAL khi nhận:
 *    byte về -> DMA chép vào rx_buf -> đường im lặng -> ngắt USARTx
 *    -> USARTx_IRQHandler() (stm32f4xx_it.c) -> HAL_UART_IRQHandler()
 *    -> HAL gọi HAL_UARTEx_RxEventCallback(huart, size)  (hàm ở cuối file này)
 *    -> ta chuyển các byte mới cho module ứng dụng qua con trỏ hàm rt->cb.
 *
 *  Luồng khi gửi:
 *    task gọi bsp_rs485_send() -> DIR = 1 -> HAL_UART_Transmit_DMA()
 *    -> task ngủ chờ semaphore -> phát xong byte cuối (cờ TC)
 *    -> HAL gọi HAL_UART_TxCpltCallback() -> DIR = 0 -> đánh thức task.
 *
 *  Sơ đồ phần cứng 3 cổng:
 *    RS485_1  USART2  HMI Kinco (Modbus RTU slave)   DMA1 Stream5 (RX) / Stream6 (TX), Channel 4
 *    RS485_2  USART3  Biến tần M1 (Modbus master)     DMA1 Stream1 (RX) / Stream3 (TX), Channel 4
 *    RS485_3  UART4   PC Vision (giao thức nhị phân)  DMA1 Stream2 (RX) / Stream4 (TX), Channel 4
 *  (Stream/Channel lấy từ bảng "DMA1 request mapping" trong Reference Manual RM0090.)
 * ---------------------------------------------------------------------------
 */
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

/* Cấu hình CỐ ĐỊNH của một cổng (nằm trong Flash, không đổi khi chạy).
 * Gom vào bảng giúp một hàm init dùng chung cho cả 3 cổng. */
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

/* Dữ liệu LÚC CHẠY của một cổng (nằm trong RAM). Chứa các HAL Handle:
 *   huart   : Handle của UART - HAL lưu trạng thái (đang gửi? đang nhận?) trong đó
 *   hdma_rx : Handle của luồng DMA nhận
 *   hdma_tx : Handle của luồng DMA gửi
 * Lưu ý: DMA1 của F407 không truy cập được vùng RAM CCM (0x10000000), nên
 * rx_buf/tx_buf phải nằm trong SRAM thường (mặc định của linker - OK).     */
typedef struct {
    UART_HandleTypeDef huart;
    DMA_HandleTypeDef  hdma_rx;
    DMA_HandleTypeDef  hdma_tx;
    SemaphoreHandle_t  tx_done;      /* "cờ" FreeRTOS: ISR báo cho task biết đã gửi xong */
    rs485_rx_cb_t      cb;           /* hàm của module ứng dụng nhận dữ liệu */
    void              *ctx;
    uint16_t           rx_pos;       /* vị trí đã xử lý tới trong rx_buf */
    bool               tx_error;
    uint8_t            rx_buf[RX_BUF_SIZE];
    uint8_t            tx_buf[TX_BUF_SIZE];
} PortRt_t;

/* Cú pháp [RS485_HMI] = {...} (designated initializer) gán phần tử theo chỉ số
 * enum, dễ đọc và không sợ nhầm thứ tự. */
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

/* Chân DIR: 1 = phát, 0 = nhận */
static inline void dir_tx(Rs485Port_t p) { s_hw[p].dir_port->BSRR = s_hw[p].dir_pin; }
static inline void dir_rx(Rs485Port_t p) { s_hw[p].dir_port->BSRR = (uint32_t)s_hw[p].dir_pin << 16U; }

/* Callback của HAL dùng CHUNG cho mọi UART và chỉ đưa vào con trỏ huart.
 * Hàm này tìm xem huart đó thuộc cổng nào của ta. */
static int port_of(const UART_HandleTypeDef *h)
{
    for (int i = 0; i < (int)RS485_PORT_COUNT; i++) {
        if (h == &s_rt[i].huart) {
            return i;
        }
    }
    return -1;
}

/** Bắt đầu (hoặc khởi động lại) việc nhận bằng DMA vòng + IDLE. */
static void start_rx(Rs485Port_t p)
{
    PortRt_t *rt = &s_rt[p];
    rt->rx_pos = 0;
    /* "Nhận tối đa RX_BUF_SIZE byte vào rx_buf, gọi callback khi đường IDLE".
     * Vì DMA ở chế độ CIRCULAR nên việc nhận kéo dài mãi, không phải gọi lại. */
    if (HAL_UARTEx_ReceiveToIdle_DMA(&rt->huart, rt->rx_buf, RX_BUF_SIZE) != HAL_OK) {
        return;
    }
    /* Mặc định HAL còn báo khi DMA đầy nửa mảng (Half Transfer). Không cần nên tắt bớt. */
    __HAL_DMA_DISABLE_IT(&rt->hdma_rx, DMA_IT_HT);
}

void bsp_rs485_init(Rs485Port_t p, uint32_t baud, Rs485Parity_t parity, rs485_rx_cb_t cb, void *ctx)
{
    const PortHw_t *hw = &s_hw[p];
    PortRt_t *rt = &s_rt[p];
    memset(rt, 0, sizeof(*rt));   /* Handle HAL phải được xóa 0 trước khi Init */
    rt->cb  = cb;
    rt->ctx = ctx;

    /* ---- Bước 1: bật xung DMA1 và UART tương ứng ---- */
    __HAL_RCC_DMA1_CLK_ENABLE();
    switch (p) {
    case RS485_HMI:    __HAL_RCC_USART2_CLK_ENABLE(); break;
    case RS485_VFD:    __HAL_RCC_USART3_CLK_ENABLE(); break;
    case RS485_VISION: __HAL_RCC_UART4_CLK_ENABLE();  break;
    default: return;
    }

    /* ---- Bước 2: chân TX/RX ở chế độ Alternate Function (nối vào UART) ---- */
    GPIO_InitTypeDef g = {0};
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;               /* giữ RX ở mức 1 (rảnh) khi không có ai phát */
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = hw->af;                    /* AF7 cho USART2/3, AF8 cho UART4 */
    g.Pin       = hw->tx_pin;
    HAL_GPIO_Init(hw->tx_port, &g);
    g.Pin = hw->rx_pin;
    HAL_GPIO_Init(hw->rx_port, &g);

    /* Chân DIR là GPIO output thường, mặc định 0 = đang nghe */
    GPIO_InitTypeDef d = {0};
    HAL_GPIO_WritePin(hw->dir_port, hw->dir_pin, GPIO_PIN_RESET);
    d.Pin   = hw->dir_pin;
    d.Mode  = GPIO_MODE_OUTPUT_PP;
    d.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(hw->dir_port, &d);

    /* ---- Bước 3: DMA nhận (ngoại vi -> bộ nhớ, vòng tròn) ---- */
    rt->hdma_rx.Instance                 = hw->dma_rx_stream;
    rt->hdma_rx.Init.Channel             = hw->dma_channel;
    rt->hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    rt->hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;     /* luôn đọc cùng thanh ghi UART->DR */
    rt->hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;      /* ghi tăng dần trong rx_buf */
    rt->hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rt->hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    rt->hdma_rx.Init.Mode                = DMA_CIRCULAR;
    rt->hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    rt->hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&rt->hdma_rx) != HAL_OK) {
        error_handler();
    }
    /* Liên kết 2 chiều UART <-> DMA: huart.hdmarx = &hdma_rx và hdma_rx.Parent = &huart.
     * Nhờ vậy HAL_UART_xxx_DMA() biết dùng luồng DMA nào. */
    __HAL_LINKDMA(&rt->huart, hdmarx, rt->hdma_rx);

    /* ---- Bước 4: DMA gửi (bộ nhớ -> ngoại vi, chạy một lượt) ---- */
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

    /* ---- Bước 5: cấu hình UART ----
     * Bẫy hay gặp: với HAL STM32F4, WordLength TÍNH CẢ bit chẵn lẻ.
     * 8 bit dữ liệu + parity (8E1 cho biến tần) => phải chọn UART_WORDLENGTH_9B. */
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

    /* Semaphore nhị phân: ISR "Give" khi gửi xong, task "Take" để chờ. */
    rt->tx_done = xSemaphoreCreateBinary();
    configASSERT(rt->tx_done != NULL);

    /* ---- Bước 6: bật ngắt trong NVIC (mức 6: được gọi API FreeRTOS ...FromISR) ---- */
    HAL_NVIC_SetPriority(hw->irq_dma_rx, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_dma_rx);
    HAL_NVIC_SetPriority(hw->irq_dma_tx, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_dma_tx);
    HAL_NVIC_SetPriority(hw->irq_uart, 6, 0);
    HAL_NVIC_EnableIRQ(hw->irq_uart);

    start_rx(p);
}

/**
 * Gửi một gói tin. Hàm CHẶN (task ngủ cho tới khi gửi xong hoặc hết thời gian),
 * chỉ gọi từ task, KHÔNG gọi trong ngắt.
 */
bool bsp_rs485_send(Rs485Port_t p, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    PortRt_t *rt = &s_rt[p];
    if (len == 0U || len > TX_BUF_SIZE) {
        return false;
    }
    /* Chép sang bộ đệm riêng: DMA đọc dần trong lúc gửi, nên vùng nhớ phải còn
     * tồn tại suốt quá trình (biến cục bộ của người gọi có thể đã mất). */
    memcpy(rt->tx_buf, data, len);
    (void)xSemaphoreTake(rt->tx_done, 0); /* xóa tín hiệu cũ còn sót */
    rt->tx_error = false;

    dir_tx(p);                            /* chuyển IC RS485 sang phát */
    bsp_delay_us(RS485_DIR_SETUP_US);     /* chờ 5 us cho IC ổn định (Spec 2.1) */
    if (HAL_UART_Transmit_DMA(&rt->huart, rt->tx_buf, len) != HAL_OK) {
        dir_rx(p);
        return false;
    }
    /* Task ngủ ở đây (CPU đi làm việc khác) cho tới khi ISR gọi Give. */
    if (xSemaphoreTake(rt->tx_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        HAL_UART_AbortTransmit(&rt->huart);
        dir_rx(p);
        return false;
    }
    return !rt->tx_error;
}

/* ------------------------------------------------------------------------ */
/* HAL callbacks (ngữ cảnh NGẮT)                                             */
/* Các hàm dưới đây ghi đè bản __weak trong HAL. HAL gọi chúng từ bên trong   */
/* HAL_UART_IRQHandler() / HAL_DMA_IRQHandler(), nên phải chạy nhanh.          */
/* ------------------------------------------------------------------------ */

/**
 * HAL gọi khi: đường truyền IDLE (hết gói), hoặc DMA chạy tới cuối mảng.
 * @p size = vị trí ghi hiện tại của DMA trong rx_buf (0..RX_BUF_SIZE).
 * Dữ liệu mới nằm từ rt->rx_pos tới size; nếu size < rx_pos tức DMA đã quay vòng.
 */
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
            /* trường hợp thường: dữ liệu mới liền một khối */
            if (rt->cb) {
                rt->cb(&rt->rx_buf[rt->rx_pos], (uint16_t)(size - rt->rx_pos), idle, rt->ctx);
            }
        } else {
            /* DMA đã quay vòng: phần cuối mảng + phần đầu mảng */
            if (rt->cb) {
                rt->cb(&rt->rx_buf[rt->rx_pos], (uint16_t)(RX_BUF_SIZE - rt->rx_pos), false, rt->ctx);
                if (size > 0U) {
                    rt->cb(&rt->rx_buf[0], size, idle, rt->ctx);
                }
            }
        }
        rt->rx_pos = size;
    } else if (idle && rt->cb) {
        rt->cb(NULL, 0, true, rt->ctx);   /* chỉ báo "hết gói", không có byte mới */
    }
    if (rt->rx_pos >= RX_BUF_SIZE) {
        rt->rx_pos = 0;
    }
}

/** HAL gọi khi byte cuối cùng đã ra khỏi chân TX hoàn toàn (cờ TC). */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    int p = port_of(huart);
    if (p < 0) {
        return;
    }
    /* Giữ DIR thêm 15 us (Spec 2.1) rồi mới chuyển về nghe. Nếu chuyển sớm,
     * bit stop cuối có thể bị cắt cụt và bên nhận báo lỗi khung. */
    bsp_delay_us(RS485_DIR_HOLD_US);
    dir_rx((Rs485Port_t)p);
    /* Đánh thức task đang chờ trong bsp_rs485_send(). Trong ngắt phải dùng
     * bản ...FromISR. Nếu task được đánh thức có ưu tiên cao hơn task đang
     * chạy, portYIELD_FROM_ISR() chuyển sang nó ngay khi thoát ngắt. */
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_rt[p].tx_done, &woken);
    portYIELD_FROM_ISR(woken);
}

/** HAL gọi khi có lỗi: nhiễu, sai khung, tràn (overrun), lỗi DMA. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    int p = port_of(huart);
    if (p < 0) {
        return;
    }
    PortRt_t *rt = &s_rt[p];
    if ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0U && huart->gState == HAL_UART_STATE_READY) {
        /* DMA gửi bị lỗi: trả DIR về nghe và đánh thức task gửi */
        rt->tx_error = true;
        dir_rx((Rs485Port_t)p);
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(rt->tx_done, &woken);
        portYIELD_FROM_ISR(woken);
    }
    /* Sau lỗi nhận, HAL đã dừng DMA nhận -> phải khởi động lại, nếu không
     * cổng sẽ "điếc" vĩnh viễn. */
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

/* Gọi từ stm32f4xx_it.c: chuyển ngắt cho HAL xử lý; HAL sẽ gọi các callback ở trên. */
void bsp_rs485_irq_uart(Rs485Port_t p)   { HAL_UART_IRQHandler(&s_rt[p].huart); }
void bsp_rs485_irq_dma_rx(Rs485Port_t p) { HAL_DMA_IRQHandler(&s_rt[p].hdma_rx); }
void bsp_rs485_irq_dma_tx(Rs485Port_t p) { HAL_DMA_IRQHandler(&s_rt[p].hdma_tx); }
