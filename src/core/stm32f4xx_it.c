/**
 * @file stm32f4xx_it.c
 * @brief Các hàm phục vụ ngắt (Interrupt Service Routine - ISR) của toàn hệ thống.
 *
 *  Ngắt hoạt động thế nào?
 *  - Mỗi nguồn ngắt có một ô trong "bảng vector" (file startup_stm32f407xx.s).
 *    Khi ngắt xảy ra, CPU tạm dừng việc đang làm và nhảy vào hàm có tên ghi
 *    trong ô đó, ví dụ USART2_IRQHandler.
 *  - File startup khai báo sẵn tất cả các tên này dạng "weak", trỏ về
 *    Default_Handler (một vòng lặp vô tận). Viết hàm trùng tên ở đây sẽ thay thế.
 *    => Viết SAI tên (ví dụ USART2_IRQHandle) thì chương trình vẫn biên dịch
 *       được nhưng khi có ngắt sẽ treo trong Default_Handler!
 *  - Ngắt chỉ được gọi khi: (1) ngoại vi bật ngắt (bit IE trong ngoại vi) và
 *    (2) NVIC cho phép (HAL_NVIC_EnableIRQ). Hai việc này làm trong các hàm init.
 *
 *  Quy ước của dự án: file này chỉ "chuyển tiếp" sang module phụ trách
 *  (bsp_xxx_irq...), để code xử lý nằm cạnh code khởi tạo của ngoại vi đó.
 *
 *  Không có ở đây:
 *  - SVC_Handler, PendSV_Handler, SysTick_Handler: do FreeRTOS cung cấp
 *    (port.c và cmsis_os2.c).
 *  - TIM7_IRQHandler (nhịp HAL): nằm trong hal_timebase_tim7.c.
 *
 *  QUY TẮC KHI VIẾT ISR: chạy thật nhanh, không gọi HAL_Delay(), không chờ đợi.
 *  Việc nặng thì báo cho task xử lý (queue, notification).
 */
#include "bsp_pulse.h"
#include "bsp_rs485.h"
#include "bsp_tick.h"
#include "board_pins.h"
#include "error_handler.h"
#include "stm32f4xx_hal.h"
#include "tracking.h"

/* ---------------- Ngoại lệ (exception) của lõi Cortex-M4 ----------------
 * HardFault thường do: truy cập con trỏ NULL/sai địa chỉ, tràn stack,
 * chia cho 0 (nếu bật bẫy). Khi gỡ lỗi, đặt breakpoint tại đây để xem
 * lỗi xảy ra ở đâu (xem thanh ghi LR/PC trong cửa sổ debug).           */
void NMI_Handler(void)        { error_handler(); }
void HardFault_Handler(void)  { error_handler(); }
void MemManage_Handler(void)  { error_handler(); }
void BusFault_Handler(void)   { error_handler(); }
void UsageFault_Handler(void) { error_handler(); }
void DebugMon_Handler(void)   {}

/* ---------------- Cảm biến lon S2 (ngắt ngoài EXTI line 2) ----------------
 * EXTI = External Interrupt: sinh ngắt khi chân GPIO có cạnh lên/xuống.
 * EXTI line 2 dùng chung cho chân số 2 của mọi port (PA2, PB2, ... PE2);
 * port nào được nối vào do HAL_GPIO_Init() cấu hình (bsp_board.c).
 *
 * Bình thường với HAL ta gọi HAL_GPIO_EXTI_IRQHandler(pin), hàm này xóa cờ
 * rồi gọi callback HAL_GPIO_EXTI_Callback(). Ở đây làm trực tiếp để đọc
 * encoder SỚM NHẤT có thể: mỗi micro-giây trễ = lon đã chạy thêm quãng đường. */
void EXTI2_IRQHandler(void)
{
    const uint32_t enc = TIM2->CNT;          /* chụp vị trí băng tải ngay lập tức */
    __HAL_GPIO_EXTI_CLEAR_IT(S2_PIN);        /* xóa cờ ngắt EXTI (bắt buộc) */
    tracking_on_s2_isr(enc);                 /* tạo bản ghi lon mới - src/app/tracking.c */
}

/* ---------------- Quét theo dõi lon 5 kHz (TIM6) ---------------- */
void TIM6_DAC_IRQHandler(void) { bsp_tick_irq(); }   /* TIM6 dùng chung vector với DAC */

/* ---------------- Bộ phát xung cho các trục động cơ ----------------
 * Mỗi lần timer tràn = đã phát xong 1 xung -> cập nhật tốc độ xung tiếp theo.
 * Một số vector dùng chung cho 2 timer (ví dụ TIM1 Update và TIM10). */
void TIM1_UP_TIM10_IRQHandler(void) { bsp_pulse_irq(AXIS_M2); }
void TIM8_UP_TIM13_IRQHandler(void) { bsp_pulse_irq(AXIS_M3); }
void TIM3_IRQHandler(void)          { bsp_pulse_irq(AXIS_M4); }
void TIM4_IRQHandler(void)          { bsp_pulse_irq(AXIS_M5); }
void TIM1_BRK_TIM9_IRQHandler(void) { bsp_pulse_irq(AXIS_M6); }

/* ---------------- 3 cổng RS485 (UART + 2 luồng DMA mỗi cổng) ----------------
 * Mỗi cổng có 3 nguồn ngắt: UART (báo IDLE/lỗi/truyền xong), DMA nhận, DMA gửi.
 * Bên trong các hàm bsp_rs485_irq_xxx() gọi HAL_UART_IRQHandler() /
 * HAL_DMA_IRQHandler(); HAL tự phân tích cờ rồi gọi callback tương ứng
 * (HAL_UARTEx_RxEventCallback, HAL_UART_TxCpltCallback...) trong bsp_rs485.c. */
void USART2_IRQHandler(void)       { bsp_rs485_irq_uart(RS485_HMI); }
void DMA1_Stream5_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_HMI); }
void DMA1_Stream6_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_HMI); }

void USART3_IRQHandler(void)       { bsp_rs485_irq_uart(RS485_VFD); }
void DMA1_Stream1_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_VFD); }
void DMA1_Stream3_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_VFD); }

void UART4_IRQHandler(void)        { bsp_rs485_irq_uart(RS485_VISION); }
void DMA1_Stream2_IRQHandler(void) { bsp_rs485_irq_dma_rx(RS485_VISION); }
void DMA1_Stream4_IRQHandler(void) { bsp_rs485_irq_dma_tx(RS485_VISION); }
