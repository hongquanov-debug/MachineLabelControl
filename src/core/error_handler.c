/**
 * @file error_handler.c
 * @brief Xử lý lỗi nghiêm trọng + các "hook" của FreeRTOS.
 *
 *  Triết lý an toàn: khi phần mềm gặp lỗi không thể phục hồi (HardFault, tràn
 *  stack, hết bộ nhớ, HAL init thất bại...) thì KHÔNG cố chạy tiếp.
 *    1. Tắt ngắt, đưa máy về trạng thái an toàn (KM1 cắt, phanh M6 đóng,
 *       tất cả van tắt, driver động cơ bị vô hiệu).
 *    2. Đứng chờ trong vòng lặp. Watchdog IWDG không được "cho ăn" nữa nên
 *       sau ~500 ms sẽ tự reset MCU (watchdog bật trong machine.c).
 *
 *  Khi gỡ lỗi bằng ST-Link: đặt breakpoint trong error_handler() rồi xem
 *  "Call Stack" để biết ai gọi tới (watchdog được cấu hình dừng khi CPU halt).
 */
#include "error_handler.h"

#include "FreeRTOS.h"
#include "board_pins.h"
#include "bsp_dio.h"
#include "io_map.h"
#include "task.h"

void error_handler(void)
{
    __disable_irq();   /* chặn MỌI ngắt (PRIMASK) - không còn gì chạy chen vào */

    /* Ghi thẳng thanh ghi BSRR thay vì HAL_GPIO_WritePin() để không phụ thuộc
     * vào bất cứ trạng thái nào của HAL (có thể đã hỏng).
     * BSRR: ghi 1 vào bit n (0..15) -> chân n lên mức 1;
     *       ghi 1 vào bit n+16      -> chân n xuống mức 0.
     * Một lệnh ghi là đủ, không cần đọc-sửa-ghi => an toàn cả trong ngắt. */
    KM1_PORT->BSRR      = (uint32_t)KM1_PIN << 16U;       /* KM1 = 0 (cắt động lực) */
    M6_BRAKE_PORT->BSRR = (uint32_t)M6_BRAKE_PIN << 16U;  /* phanh M6 = 0 (đóng phanh) */

    /* 24 ngõ ra qua 74HCT595: van tắt, driver bị vô hiệu (DO_SAFE_IMAGE) */
    for (uint8_t i = 0; i < DIO_DO_COUNT; i++) {
        bsp_dio_do_write(i, (DO_SAFE_IMAGE >> i) & 1U);
    }
    bsp_dio_do_commit();
    for (;;) {
        /* chờ watchdog IWDG reset */
    }
}

/* Gọi từ macro configASSERT() (FreeRTOSConfig.h) khi một điều kiện kiểm tra
 * của FreeRTOS sai - thường do dùng sai API (ví dụ gọi hàm không có đuôi
 * "FromISR" bên trong ngắt, hoặc mức ưu tiên ngắt không hợp lệ).             */
void app_rtos_assert_failed(const char *file, int line)
{
    (void)file;   /* khi debug: xem giá trị file/line để biết chỗ lỗi */
    (void)line;
    error_handler();
}

/* FreeRTOS gọi khi phát hiện một task dùng quá vùng stack được cấp
 * (configCHECK_FOR_STACK_OVERFLOW = 2). Cách sửa: tăng stack_bytes của task
 * đó trong bảng k_tasks[] (src/app/app_main.c).                             */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;   /* name = tên task bị tràn stack */
    error_handler();
}

/* FreeRTOS gọi khi heap (configTOTAL_HEAP_SIZE) không đủ để tạo task/queue. */
void vApplicationMallocFailedHook(void)
{
    error_handler();
}
