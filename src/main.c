/**
 * @file main.c
 * @brief PMeMS CONTROLLER 1 - Can orientation & lid labeling line (Nam Viet F&B).
 *
 *  MCU : STM32F407VET6 @ 168 MHz
 *  SDK : STM32Cube HAL + FreeRTOS (CMSIS-RTOS v2)
 *  Spec: docs/FIRMWARE_PROCESS_SPEC.md REV 3.2
 *
 * ============================================================================
 *  TRÌNH TỰ KHỞI ĐỘNG CỦA CHƯƠNG TRÌNH (đọc file này đầu tiên)
 * ============================================================================
 *  1. Cấp nguồn / Reset -> CPU nhảy vào Reset_Handler (file startup_stm32f407xx.s
 *     do PlatformIO cung cấp). Reset_Handler gọi SystemInit() (bật FPU, ...),
 *     chép biến khởi tạo từ Flash sang RAM, xóa vùng .bss rồi gọi main().
 *  2. main() chỉ làm phần TỐI THIỂU trước khi chạy hệ điều hành (RTOS):
 *       - HAL_Init()            : khởi tạo thư viện HAL
 *       - system_clock_config() : nâng xung nhịp lên 168 MHz
 *       - bsp_board_init()      : đặt các chân GPIO về trạng thái AN TOÀN
 *       - tạo task "init" rồi khởi động FreeRTOS (osKernelStart)
 *  3. Task "init" (src/app/app_main.c) khởi tạo phần còn lại (UART, EEPROM,
 *     encoder, ...) rồi tạo các task ứng dụng.
 *
 *  Vì sao không khởi tạo hết trong main()?
 *  -> Sau lời gọi API FreeRTOS đầu tiên (ví dụ tạo queue), FreeRTOS cố ý CHE
 *     các ngắt cho tới khi scheduler chạy. Khi đó HAL_Delay() (dựa vào ngắt
 *     TIM7) sẽ treo mãi. Vì vậy mọi thứ cần HAL_Delay/timeout được làm trong
 *     task "init", lúc scheduler đã chạy.
 * ============================================================================
 */
#include "app_main.h"
#include "bsp_board.h"
#include "cmsis_os2.h"      /* API CMSIS-RTOS v2 (lớp bọc chuẩn ARM quanh FreeRTOS) */
#include "error_handler.h"
#include "stm32f4xx_hal.h"  /* header chính của HAL: tự include các module bật trong stm32f4xx_hal_conf.h */
#include "system_clock.h"

int main(void)
{
    /* HAL_Init():
     *  - bật bộ đệm Flash (prefetch, cache lệnh/dữ liệu)
     *  - đặt nhóm ưu tiên ngắt NVIC = 4 (4 bit ưu tiên chính, 0 bit phụ)
     *  - gọi HAL_InitTick() để tạo "nhịp" 1 ms cho HAL_GetTick()/HAL_Delay().
     *    Ở dự án này HAL_InitTick() được viết lại để dùng TIM7 thay cho SysTick
     *    (SysTick dành cho FreeRTOS) - xem src/core/hal_timebase_tim7.c          */
    HAL_Init();

    /* Sau reset chip chạy bằng dao động nội HSI 16 MHz. Hàm này chuyển sang
     * thạch anh ngoài HSE 8 MHz + PLL để chạy 168 MHz (src/core/system_clock.c). */
    system_clock_config();

    /* Đặt ngay các ngõ ra quan trọng về trạng thái an toàn:
     * KM1 (contactor động lực) TẮT, phanh M6 ĐÓNG, đèn/còi tắt.            */
    bsp_board_init();

    /* Khởi tạo nhân FreeRTOS (chưa chạy task nào). */
    if (osKernelInitialize() != osOK) {
        error_handler();
    }

    /* Tạo task "init" - xem app_create() trong src/app/app_main.c */
    app_create();

    /* Giao quyền điều khiển CPU cho FreeRTOS. Hàm này KHÔNG BAO GIỜ trả về
     * nếu thành công: từ đây CPU chỉ chạy các task và các hàm ngắt.        */
    (void)osKernelStart();

    /* Chỉ tới được đây nếu FreeRTOS không khởi động được (thiếu RAM heap). */
    error_handler();
}
