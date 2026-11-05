/**
 * @file stm32f4xx_it.c
 * @brief Interrupt vector implementations. Peripheral handlers delegate to BSP/app modules.
 *        SVC/PendSV/SysTick are provided by FreeRTOS (port.c / cmsis_os2.c).
 *        TIM7 (HAL timebase) is in hal_timebase_tim7.c.
 */
#include "bsp_pulse.h"
#include "bsp_rs485.h"
#include "bsp_tick.h"
#include "board_pins.h"
#include "error_handler.h"
#include "stm32f4xx_hal.h"
#include "tracking.h"

/* ---------------- Cortex-M4 exceptions ---------------- */
void NMI_Handler(void)        { error_handler(); }
void HardFault_Handler(void)  { error_handler(); }
void MemManage_Handler(void)  { error_handler(); }
void BusFault_Handler(void)   { error_handler(); }
void UsageFault_Handler(void) { error_handler(); }
void DebugMon_Handler(void)   {}

/* ---------------- Can sensor S2 (EXTI2) ---------------- */
void EXTI2_IRQHandler(void)
{
    const uint32_t enc = TIM2->CNT;          /* capture first: latency-critical */
    __HAL_GPIO_EXTI_CLEAR_IT(S2_PIN);
    tracking_on_s2_isr(enc);
}

/* ---------------- Tracking scan 5 kHz ---------------- */
void TIM6_DAC_IRQHandler(void) { bsp_tick_irq(); }

/* ---------------- Pulse generators ---------------- */
void TIM1_UP_TIM10_IRQHandler(void) { bsp_pulse_irq(AXIS_M2); }
void TIM8_UP_TIM13_IRQHandler(void) { bsp_pulse_irq(AXIS_M3); }
void TIM3_IRQHandler(void)          { bsp_pulse_irq(AXIS_M4); }
void TIM4_IRQHandler(void)          { bsp_pulse_irq(AXIS_M5); }
void TIM1_BRK_TIM9_IRQHandler(void) { bsp_pulse_irq(AXIS_M6); }

/* ---------------- RS485 ports ---------------- */
void USART2_IRQHandler(void)       { bsp_rs485_irq_uart(RS485_HMI); }
void DMA1_Stream5_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_HMI); }
void DMA1_Stream6_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_HMI); }

void USART3_IRQHandler(void)       { bsp_rs485_irq_uart(RS485_VFD); }
void DMA1_Stream1_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_VFD); }
void DMA1_Stream3_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_VFD); }

void UART4_IRQHandler(void)        { bsp_rs485_irq_uart(RS485_VISION); }
void DMA1_Stream2_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_VISION); }
void DMA1_Stream4_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_VISION); }
