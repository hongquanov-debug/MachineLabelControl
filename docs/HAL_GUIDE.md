# Hướng dẫn đọc code cho người mới dùng STM32 HAL

Tài liệu này giải thích các khái niệm HAL và FreeRTOS **xuất hiện trong dự án này**,
kèm chỗ để xem ví dụ thật trong code. Nên đọc theo thứ tự từ trên xuống.

---

## 1. Thứ tự nên đọc code

| Bước | File | Học được gì |
|---|---|---|
| 1 | `src/main.c` | Chương trình khởi động thế nào |
| 2 | `src/core/system_clock.c` | Cấu hình xung nhịp 168 MHz, "Init struct" của HAL |
| 3 | `src/bsp/bsp_board.c` | GPIO: ngõ vào, ngõ ra, ngắt ngoài EXTI |
| 4 | `src/core/hal_timebase_tim7.c` | Timer, ngắt, hàm `__weak` của HAL |
| 5 | `src/core/stm32f4xx_it.c` | Bảng vector ngắt, cách ngắt đến được code của ta |
| 6 | `src/bsp/bsp_encoder.c` | HAL dùng "Handle" (`TIM_HandleTypeDef`) |
| 7 | `src/bsp/bsp_rs485.c` | UART + DMA + callback: phần HAL khó nhất trong dự án |
| 8 | `src/bsp/bsp_eeprom.c` | I2C kiểu blocking, mutex FreeRTOS |
| 9 | `src/bsp/bsp_pulse.c` | Timer PWM phát xung động cơ, ghi thanh ghi trực tiếp |
| 10 | `src/app/app_main.c` → `io.c` → `tracking.c` → `machine.c` | Logic máy, task FreeRTOS |

---

## 2. Các khái niệm HAL

### 2.1. Bật xung ngoại vi (clock enable)

Sau reset, **mọi ngoại vi đều tắt xung** để tiết kiệm điện. Phải bật trước khi dùng:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
__HAL_RCC_USART2_CLK_ENABLE();
```

Quên bước này là lỗi phổ biến nhất: code cấu hình đúng hết nhưng ngoại vi "không chạy".

### 2.2. Init struct

Muốn cấu hình một thứ gì đó: khai báo struct `XxxInitTypeDef`, điền các trường rồi gọi hàm Init.

```c
GPIO_InitTypeDef g = {0};        // = {0}: các trường không điền sẽ bằng 0
g.Pin  = GPIO_PIN_5;
g.Mode = GPIO_MODE_OUTPUT_PP;
HAL_GPIO_Init(GPIOA, &g);
```

Ví dụ: `bsp_board.c` (GPIO), `system_clock.c` (RCC).

### 2.3. Handle

Với ngoại vi phức tạp (UART, TIM, I2C, SPI, DMA), HAL dùng một **Handle** — một struct
chứa cả cấu hình (`.Init`) lẫn trạng thái lúc chạy (đang gửi, đang nhận, mã lỗi...).

```c
static UART_HandleTypeDef huart;   // phải là static/toàn cục, KHÔNG để trên stack
huart.Instance       = USART2;     // trỏ tới khối thanh ghi phần cứng
huart.Init.BaudRate  = 19200;
HAL_UART_Init(&huart);
HAL_UART_Transmit(&huart, data, len, 100);   // mọi hàm sau đều truyền &huart
```

Ví dụ: `bsp_encoder.c` (`htim2`), `bsp_rs485.c` (`huart`, `hdma_rx`, `hdma_tx`), `bsp_eeprom.c` (`s_i2c`).

### 2.4. Giá trị trả về `HAL_StatusTypeDef`

Hầu hết hàm HAL trả về `HAL_OK`, `HAL_ERROR`, `HAL_BUSY` hoặc `HAL_TIMEOUT`. Luôn kiểm tra:

```c
if (HAL_UART_Init(&huart) != HAL_OK) {
    error_handler();
}
```

### 2.5. Ba kiểu giao tiếp: Blocking, Interrupt, DMA

| Kiểu | Hàm ví dụ | Đặc điểm | Dùng trong dự án |
|---|---|---|---|
| Blocking | `HAL_I2C_Mem_Read(..., timeout)` | Chờ tới khi xong, CPU bị chiếm | EEPROM, W5500 |
| Interrupt | `HAL_UART_Transmit_IT()` | Trả về ngay, mỗi byte một ngắt | (không dùng) |
| DMA | `HAL_UART_Transmit_DMA()` | Trả về ngay, DMA tự chép dữ liệu, chỉ ngắt khi xong | 3 cổng RS485 |

### 2.6. Đường đi của một ngắt và callback

Ví dụ khi UART2 nhận xong một gói:

```
Phần cứng USART2 báo IDLE
  → CPU nhảy vào USART2_IRQHandler()           (src/core/stm32f4xx_it.c)
  → bsp_rs485_irq_uart() → HAL_UART_IRQHandler(&huart)   (code thư viện HAL)
  → HAL kiểm tra cờ, gọi HAL_UARTEx_RxEventCallback()    (src/bsp/bsp_rs485.c)
  → code của ta xử lý dữ liệu
```

Các hàm callback (`HAL_UART_TxCpltCallback`, `HAL_UART_ErrorCallback`...) được HAL khai báo
`__weak`. Viết một hàm **trùng tên** trong code của ta thì nó thay thế bản mặc định.
**Callback dùng chung cho mọi UART**, nên phải kiểm tra tham số `huart` để biết là cổng nào
(xem `port_of()` trong `bsp_rs485.c`).

Hai việc phải làm để có ngắt:
1. Bật ngắt trong ngoại vi (HAL làm khi gọi hàm `_IT`/`_DMA`, hoặc bit `DIER` của timer).
2. Bật trong NVIC: `HAL_NVIC_SetPriority(IRQn, prio, 0); HAL_NVIC_EnableIRQ(IRQn);`

### 2.7. Alternate Function (AF)

Một chân có thể là GPIO thường hoặc được nối vào ngoại vi (UART, timer...).
Khi đó dùng `GPIO_MODE_AF_PP` (hoặc `GPIO_MODE_AF_OD` cho I2C) và chọn đúng số AF theo
bảng trong datasheet:

```c
g.Mode      = GPIO_MODE_AF_PP;
g.Alternate = GPIO_AF7_USART2;   // PD5/PD6 làm TX/RX của USART2
```

### 2.8. Khi nào ghi thẳng thanh ghi thay vì dùng HAL?

Dự án có chỗ dùng `TIM6->ARR = ...` hoặc `GPIOD->BSRR = ...` thay vì hàm HAL. Lý do:
- **Tốc độ**: trong ngắt chạy 150 000 lần/giây (`bsp_pulse.c`), gọi hàm HAL quá chậm.
- **Đơn giản**: timer chỉ cần báo tràn định kỳ (`bsp_tick.c`); vài dòng thanh ghi gọn hơn
  cả bộ `HAL_TIM_Base_Init` + callback.

Tên thanh ghi (`CR1`, `PSC`, `ARR`, `SR`, `BSRR`...) trùng với **Reference Manual RM0090**.
Gặp tên lạ thì tra trong tài liệu này.

| Thanh ghi GPIO | Ý nghĩa |
|---|---|
| `IDR` | Đọc mức logic của 16 chân |
| `BSRR` | Ghi 1 vào bit n → chân n lên 1; ghi 1 vào bit n+16 → chân n xuống 0 |

| Thanh ghi Timer | Ý nghĩa |
|---|---|
| `PSC` | Bộ chia: tần số đếm = xung timer / (PSC+1) |
| `ARR` | Đếm tới ARR thì tràn (chu kỳ = ARR+1) |
| `CNT` | Giá trị đếm hiện tại (encoder: vị trí băng tải) |
| `CCR1` | So sánh kênh 1 (độ rộng xung PWM) |
| `SR` / `DIER` | Cờ ngắt / cho phép ngắt |

### 2.9. Xung nhịp của timer

Timer trên bus APB nhận xung **gấp đôi** PCLK khi bộ chia APB khác 1:
- TIM2..TIM7, TIM3, TIM4 (APB1): **84 MHz**
- TIM1, TIM8, TIM9 (APB2): **168 MHz**

Xem sơ đồ đầy đủ trong `src/core/system_clock.c`.

---

## 3. FreeRTOS trong 5 phút

FreeRTOS cho phép viết nhiều "chương trình con" (task) chạy như song song.
Mỗi task là một hàm có vòng lặp vô tận:

```c
void io_task(void *arg) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        // ... làm việc ...
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1));   // ngủ tới mốc 1 ms kế tiếp
    }
}
```

| Khái niệm | Dùng để | Ví dụ trong dự án |
|---|---|---|
| Task + ưu tiên | Việc gấp chạy trước | Bảng `k_tasks[]` trong `app_main.c` |
| `vTaskDelay` / `vTaskDelayUntil` | Ngủ, nhường CPU (thay cho `HAL_Delay`) | `io.c`, `machine.c` |
| Queue (hàng đợi) | Gửi dữ liệu giữa ngắt/task | `tracking.c` → task trạm |
| Semaphore | Báo hiệu "đã xong" | `tx_done` trong `bsp_rs485.c` |
| Mutex | Chỉ một task dùng tài nguyên tại một lúc | `bsp_eeprom.c`, `recipe.c` |
| StreamBuffer | Truyền chuỗi byte từ ngắt sang task | `vision.c` |
| Task notification | Đánh thức một task cụ thể | `hmi_link.c`, `vfd.c` |

### Hai quy tắc quan trọng nhất

1. **Trong ngắt chỉ được gọi hàm có đuôi `FromISR`**: `xQueueSendFromISR`,
   `xSemaphoreGiveFromISR`... Gọi bản thường trong ngắt sẽ làm hỏng hệ thống.
2. **Ngắt muốn gọi `...FromISR` phải có mức ưu tiên NVIC từ 5 tới 15**
   (`configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`). Ngắt mức 0..4 nhanh hơn nhưng không được
   gọi API FreeRTOS. Trong dự án:

| Mức NVIC | Ngắt | Gọi FreeRTOS? |
|---|---|---|
| 2 | Phát xung động cơ (TIM1/8/3/4/9) | Không |
| 5 | Cảm biến S2 (EXTI2), quét tracking (TIM6) | Có (`FromISR`) |
| 6 | UART, DMA | Có (`FromISR`) |
| 15 | HAL tick (TIM7), FreeRTOS (SysTick, PendSV) | — |

> Lưu ý: ưu tiên **task** thì số càng **lớn** càng ưu tiên; ưu tiên **ngắt NVIC** thì số càng
> **nhỏ** càng ưu tiên.

### Vì sao không dùng `HAL_Delay()` trong task?

`HAL_Delay()` chờ bận: CPU quay vòng không làm gì, các task ưu tiên thấp hơn bị "đói".
`vTaskDelay()` cho task ngủ thật sự, CPU đi làm việc khác.

---

## 4. Dữ liệu dùng chung giữa ngắt và task

Biến bị sửa trong ngắt và đọc/sửa trong task:
- khai báo `volatile` để trình biên dịch luôn đọc lại từ RAM;
- khi đọc-sửa-ghi thì bọc trong vùng găng `crit_enter()` / `crit_exit()` (`src/core/crit.h`).

```c
uint32_t s = crit_enter();
s_do_image |= (1UL << index);
crit_exit(s);
```

---

## 5. Lỗi thường gặp & cách tìm

| Hiện tượng | Nguyên nhân hay gặp |
|---|---|
| Ngoại vi không phản hồi | Quên `__HAL_RCC_xxx_CLK_ENABLE()` |
| Treo khi có ngắt | Tên hàm `xxx_IRQHandler` viết sai → rơi vào `Default_Handler` |
| Ngắt chạy liên tục, chương trình đứng | Quên xóa cờ ngắt trong ISR |
| TIM1/TIM8 không ra xung | Quên bật `MOE` (thanh ghi `BDTR`) |
| UART parity sai dữ liệu | Có parity thì `WordLength` phải là `UART_WORDLENGTH_9B` |
| `HAL_Delay()` treo lúc khởi động | Gọi sau API FreeRTOS nhưng trước `osKernelStart()` |
| Dừng ở `error_handler()` | Đặt breakpoint, xem Call Stack (HardFault, tràn stack, hết heap) |
| Lỗi build `vstmdbeq ... not supported` | Thiếu cờ FPU → xem `scripts/fpu.py` |

---

## 6. Tài liệu tham khảo

- **RM0090** – Reference Manual STM32F405/407: mô tả mọi thanh ghi.
- **DS8626** – Datasheet STM32F407: sơ đồ chân, bảng Alternate Function.
- **UM1725** – Description of STM32F4 HAL and LL drivers: mô tả từng hàm HAL.
- Mã nguồn HAL (`stm32f4xx_hal_uart.c`...): đầu mỗi file có mục "How to use this driver".
  Trong VS Code, giữ Ctrl rồi click tên hàm HAL để mở định nghĩa.
- FreeRTOS: https://www.freertos.org/Documentation
