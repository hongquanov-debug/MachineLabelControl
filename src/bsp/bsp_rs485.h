/**
 * @file bsp_rs485.h
 * @brief Half-duplex RS485 ports with DMA circular RX + IDLE line detection,
 *        DMA TX and automatic DIR (DE/RE) control (Spec §2.1, §7).
 *
 *   RS485_1  USART2  HMI Kinco (Modbus RTU slave)   DMA1 S5/S6 Ch4
 *   RS485_2  USART3  VFD M1   (Modbus RTU master)   DMA1 S1/S3 Ch4
 *   RS485_3  UART4   Vision PC (binary protocol)    DMA1 S2/S4 Ch4
 */
#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RS485_HMI = 0,
    RS485_VFD,
    RS485_VISION,
    RS485_PORT_COUNT
} Rs485Port_t;

typedef enum {
    RS485_PARITY_NONE = 0,
    RS485_PARITY_EVEN,
    RS485_PARITY_ODD
} Rs485Parity_t;

/**
 * RX callback, called in ISR context (NVIC prio 6) with newly received bytes.
 * @p idle is true when the chunk ends with an IDLE line (= end of frame).
 */
typedef void (*rs485_rx_cb_t)(const uint8_t *data, uint16_t len, bool idle, void *ctx);

/** Must be called before the scheduler starts (creates the TX semaphore). */
void bsp_rs485_init(Rs485Port_t port, uint32_t baud, Rs485Parity_t parity,
                    rs485_rx_cb_t cb, void *ctx);

/** Blocking send from task context (DMA). Returns false on timeout/error. */
bool bsp_rs485_send(Rs485Port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* IRQ dispatch (called from stm32f4xx_it.c) */
void bsp_rs485_irq_uart(Rs485Port_t port);
void bsp_rs485_irq_dma_rx(Rs485Port_t port);
void bsp_rs485_irq_dma_tx(Rs485Port_t port);

#endif /* BSP_RS485_H */
