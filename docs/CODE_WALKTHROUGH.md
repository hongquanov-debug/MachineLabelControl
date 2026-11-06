# Giải thích chi tiết mã nguồn – PMeMS Controller 1

> Tài liệu này đi qua **toàn bộ** dự án: cấu trúc thư mục, vai trò từng file, giải thích
> từng cụm code quan trọng và các luồng hoạt động từ đầu tới cuối.
> Đọc kèm: [`HAL_GUIDE.md`](HAL_GUIDE.md) (khái niệm HAL/FreeRTOS cơ bản),
> [`FIRMWARE_PROCESS_SPEC.md`](FIRMWARE_PROCESS_SPEC.md) (đặc tả quy trình),
> [`SPEC_NOTES.md`](SPEC_NOTES.md) (điểm mâu thuẫn trong spec & chân cần xác nhận).

## Mục lục

1. [Bức tranh tổng thể](#1-bức-tranh-tổng-thể)
2. [Cấu trúc thư mục](#2-cấu-trúc-thư-mục)
3. [Kiến trúc phân lớp và quy tắc phụ thuộc](#3-kiến-trúc-phân-lớp-và-quy-tắc-phụ-thuộc)
4. [Hệ thống build: `platformio.ini` và `scripts/`](#4-hệ-thống-build-platformioini-và-scripts)
5. [Thư mục `include/` – cấu hình toàn cục](#5-thư-mục-include--cấu-hình-toàn-cục)
6. [Thư viện lõi `lib/pmems_core/`](#6-thư-viện-lõi-libpmems_core)
7. [`src/main.c` và `src/core/`](#7-srcmainc-và-srccore)
8. [Lớp phần cứng `src/bsp/`](#8-lớp-phần-cứng-srcbsp)
9. [Lớp ứng dụng `src/app/`](#9-lớp-ứng-dụng-srcapp)
10. [Các luồng hoạt động từ đầu tới cuối](#10-các-luồng-hoạt-động-từ-đầu-tới-cuối)
11. [Kiểm thử `test/` và công cụ PC `tools/pc_config/`](#11-kiểm-thử-test-và-công-cụ-pc-toolspc_config)
12. [Hướng dẫn sửa / mở rộng thường gặp](#12-hướng-dẫn-sửa--mở-rộng-thường-gặp)
13. [Bảng tra cứu nhanh](#13-bảng-tra-cứu-nhanh)

---

## 1. Bức tranh tổng thể

### 1.1. Máy làm gì?

Lon chạy trên băng tải dài 2 m. Trên đường đi, mỗi lon qua lần lượt các trạm:

```
   cấp lon        S2        Camera     Chỉnh góc      Dán tem       Kiểm tra    Loại lon      Cuối
  (P1/P2)    (cảm biến)    (+50mm)     (+300mm)       (+650mm)      (+950mm)    (+1250mm)   (+2000mm)
  ───●──────────●────────────●────────────●──────────────●─────────────●───────────●───────────●──►
                │            │            │              │             │           │
             ghi nhận     chụp ảnh,    P3 chặn,       mâm 6 đầu     cảm biến    xi lanh P8
             lon mới,     PC Vision    P4 kẹp,        hút M3,       S5 xem      gạt lon lỗi
             đọc encoder  trả về góc θ M2 quay θ      xi lanh P5    có tem chưa ra máng
```

Bộ điều khiển STM32F407 phải:
- biết **mỗi lon đang ở đâu** (nhờ encoder trên trục băng tải);
- kích từng trạm **đúng lúc** lon tới;
- điều khiển 5 trục động cơ (M2..M6), ~20 xi lanh/van, đọc ~40 cảm biến;
- nói chuyện với PC Vision (RS485), HMI (RS485), biến tần (RS485), PC/SCADA (Ethernet);
- đảm bảo an toàn (E-Stop, cửa, khí nén, lỗi driver).

### 1.2. Sơ đồ khối phần cứng ↔ phần mềm

```
                    ┌──────────────────────── STM32F407 ────────────────────────┐
 Encoder băng tải ──┤ TIM2 (encoder)      ─► bsp_encoder ─┐                     │
 Cảm biến S2 ───────┤ EXTI2               ─► tracking ◄───┤  (ngắt TIM6 5 kHz)  │
 32 DI (74HC165) ───┤ GPIO bit-bang       ─► bsp_dio ─► io (task 1 ms)          │
 24 DO (74HC595) ◄──┤ GPIO bit-bang       ◄─ bsp_dio ◄─ io / các trạm           │
 Driver M2..M6 ◄────┤ TIM1/8/3/4/9 PWM    ◄─ bsp_pulse ◄─ motion ◄─ trạm/homing │
 PC Vision ◄───────►┤ UART4  + DMA        ◄► bsp_rs485 ◄► vision                │
 HMI Kinco ◄───────►┤ USART2 + DMA        ◄► bsp_rs485 ◄► hmi_link ◄► modbus_regs
 Biến tần M1 ◄─────►┤ USART3 + DMA        ◄► bsp_rs485 ◄► vfd                   │
 PC / SCADA ◄──────►┤ SPI2 → W5500        ◄► bsp_w5500 ◄► net_link ◄► modbus_regs
 EEPROM AT24C64 ◄──►┤ I2C1                ◄► bsp_eeprom ◄► recipe               │
 E-Stop/cửa/khí ────┤ GPIO trực tiếp      ─► bsp_board ─► io ─► machine (FSM)   │
 KM1, phanh M6 ◄────┤ GPIO trực tiếp      ◄─ bsp_board ◄─ machine / io          │
                    └────────────────────────────────────────────────────────────┘
```

---

## 2. Cấu trúc thư mục

```
MachineLabelControl/
├── platformio.ini                  (60)  Cấu hình build: 2 môi trường controller1 + native
├── README.md                             Giới thiệu, lệnh build/nạp/test
├── .gitignore, .vscode/                  Bỏ qua file build; gợi ý cài extension PlatformIO
│
├── scripts/                              Script Python chạy trong lúc build (PlatformIO/SCons)
│   ├── fpu.py                      (27)  Bật FPU phần cứng (-mfpu, -mfloat-abi=hard)
│   └── freertos.py                 (51)  Biên dịch FreeRTOS từ gói framework STM32Cube
│
├── include/                              Header cấu hình dùng chung toàn dự án
│   ├── app_config.h               (178)  MỌI hằng số quy trình: tọa độ trạm, thời gian, động học
│   ├── FreeRTOSConfig.h            (98)  Cấu hình nhân FreeRTOS, chính sách ưu tiên ngắt
│   └── stm32f4xx_hal_conf.h       (504)  Bật/tắt module HAL, tần số thạch anh
│
├── lib/pmems_core/                       Thư viện THUẦN C - không đụng phần cứng, test được trên PC
│   ├── library.json                      Khai báo thư viện cho PlatformIO
│   ├── include/  src/
│   │   ├── crc16                  (17)   CRC16 Modbus
│   │   ├── can_record + can_fifo  (52)   Bản ghi một lon + hàng đợi FIFO 32 lon
│   │   ├── vision_proto          (107)   Đóng/giải gói tin với PC Vision
│   │   ├── modbus_pdu            (207)   Xử lý Modbus (slave RTU/TCP, master RTU)
│   │   ├── pulse_ramp             (89)   Tính tốc độ tăng/giảm tốc hình thang theo từng xung
│   │   ├── orient_math            (24)   Tính số xung M2 để xoay lon đúng góc
│   │   ├── recipe_types           (83)   Struct Recipe 64 byte, CRC, giá trị mặc định
│   │   └── recipe_regs            (55)   Recipe ↔ thanh ghi Modbus (cho PC cấu hình)
│
├── src/
│   ├── main.c                     (69)   Điểm vào chương trình
│   ├── core/                             Lõi hệ thống (không thuộc về thiết bị nào cụ thể)
│   │   ├── system_clock.c/.h      (71)   Cấu hình xung nhịp 168 MHz
│   │   ├── hal_timebase_tim7.c    (78)   Nhịp 1 ms của HAL chạy trên TIM7
│   │   ├── stm32f4xx_it.c         (88)   Tất cả hàm phục vụ ngắt (ISR)
│   │   ├── crit.h                 (44)   Vùng găng (chặn ngắt tạm thời)
│   │   └── error_handler.c/.h     (69)   Lỗi nghiêm trọng → trạng thái an toàn
│   ├── bsp/                              Board Support Package - driver phần cứng
│   │   ├── board_pins.h          (164)   Bảng gán chân MCU
│   │   ├── bsp_board.c/.h        (144)   GPIO: an toàn, home, camera, delay us
│   │   ├── bsp_encoder.c/.h       (67)   Encoder băng tải (TIM2)
│   │   ├── bsp_tick.c/.h          (50)   Ngắt định kỳ 5 kHz (TIM6)
│   │   ├── bsp_dio.c/.h          (176)   32 DI / 24 DO qua IC thanh ghi dịch
│   │   ├── bsp_rs485.c/.h        (366)   3 cổng RS485 (UART + DMA)
│   │   ├── bsp_pulse.c/.h        (259)   Phát xung 5 trục động cơ
│   │   ├── bsp_eeprom.c/.h       (138)   EEPROM AT24C64 (I2C1)
│   │   └── bsp_w5500.c/.h        (219)   Chip Ethernet W5500 (SPI2)
│   └── app/                              Logic quy trình của máy
│       ├── app_main.c/.h         (117)   Khởi tạo và tạo các task
│       ├── io_map.h               (53)   Tên 32 DI / 24 DO
│       ├── io.c/.h               (167)   Task I/O 1 ms + giám sát an toàn
│       ├── alarm.c/.h             (88)   Bảng mã alarm + mức độ nghiêm trọng
│       ├── tracking.c/.h         (300)   Theo dõi lon theo encoder (TRUNG TÂM của quy trình)
│       ├── vision.c/.h            (99)   Liên kết PC Vision
│       ├── station_feed.c/.h      (65)   Trạm cấp lon P1/P2
│       ├── station_orient.c/.h   (121)   Trạm chỉnh góc P3/P4/M2
│       ├── station_label.c/.h    (285)   Trạm dán tem 6 đầu hút
│       ├── motion.c/.h           (204)   Lớp trục: đơn vị, giới hạn, homing từng trục
│       ├── homing.c/.h           (113)   Trình tự về gốc toàn máy
│       ├── machine.c/.h          (444)   Máy trạng thái hệ thống (FSM)
│       ├── recipe.c/.h           (177)   Đọc/ghi recipe trên EEPROM
│       ├── params.c/.h            (14)   Tham số chỉnh lúc chạy (tốc độ, offset, jog)
│       ├── modbus_regs.c/.h      (237)   Bản đồ thanh ghi Modbus dùng chung HMI/PC
│       ├── hmi_link.c/.h          (65)   Modbus RTU slave cho HMI
│       ├── vfd.c/.h              (142)   Modbus RTU master điều khiển biến tần
│       └── net_link.c/.h          (75)   Modbus TCP server (Ethernet)
│
├── test/test_core/test_main.c    (312)   16 unit test cho lib/pmems_core (Unity)
├── tools/pc_config/                      Thư viện + dòng lệnh Python cho PC cấu hình máy
└── docs/                                 Tài liệu (bạn đang đọc)
```

*(Số trong ngoặc = số dòng của file `.c` chính.)*

---

## 3. Kiến trúc phân lớp và quy tắc phụ thuộc

```
┌─────────────────────────────────────────────────────────────┐
│  src/app/      LOGIC MÁY   (tracking, trạm, FSM, Modbus...)  │  biết "quy trình"
├─────────────────────────────────────────────────────────────┤
│  src/bsp/      DRIVER      (rs485, pulse, dio, eeprom...)    │  biết "phần cứng board"
├─────────────────────────────────────────────────────────────┤
│  STM32 HAL + CMSIS + FreeRTOS   (thư viện của ST / ARM)      │  biết "chip"
└─────────────────────────────────────────────────────────────┘
        ▲ cả app và bsp đều được dùng lib/pmems_core (thuần C)
```

Quy tắc:
- `app` gọi `bsp`; `bsp` **không** include file nào của `app` (trừ `stm32f4xx_it.c` và
  `error_handler.c` ở `core/` có nhiệm vụ "nối dây").
- `lib/pmems_core` **không** include HAL hay FreeRTOS → biên dịch và test được trên máy tính.
- Mọi con số của quy trình nằm ở `include/app_config.h`, mọi chân MCU nằm ở
  `src/bsp/board_pins.h`. Đổi phần cứng hay thông số không cần sửa logic.

Quy ước đặt tên:

| Mẫu | Ý nghĩa | Ví dụ |
|---|---|---|
| `bsp_xxx_...` | Hàm driver phần cứng | `bsp_rs485_send()` |
| `xxx_task` | Hàm thân của một task FreeRTOS | `io_task()` |
| `..._isr` / `..._irq` | Chạy trong ngữ cảnh ngắt | `tracking_tick_isr()` |
| `s_xxx` | Biến `static` (chỉ dùng trong file) | `s_fifo`, `s_state` |
| `k_xxx` | Bảng hằng (`static const`) | `k_tasks[]`, `k_axis[]` |
| `XxxYyy_t` | Kiểu dữ liệu (`typedef`) | `CanRecord_t`, `Axis_t` |
| `TODO(HW)` | Giá trị giả định, cần kiểm tra trên máy thật | xem `SPEC_NOTES.md` |

---

## 4. Hệ thống build: `platformio.ini` và `scripts/`

### 4.1. `platformio.ini`

```ini
[env:controller1]
platform = ststm32                       ; gói hỗ trợ chip STM32
board = genericSTM32F407VET6             ; chip cụ thể → linker script 512K Flash / 128K RAM
framework = stm32cube                    ; dùng thư viện HAL chính hãng ST
board_build.stm32cube.custom_config_header = yes   ; dùng include/stm32f4xx_hal_conf.h của ta
extra_scripts =
    pre:scripts/fpu.py                   ; chạy TRƯỚC khi build
    pre:scripts/freertos.py
custom_float_abi = hard                  ; tùy chọn riêng, đọc bởi fpu.py
custom_freertos_heap = heap_4            ; tùy chọn riêng, đọc bởi freertos.py
build_flags = -std=gnu11 -Iinclude -Isrc/core -Isrc/bsp -Isrc/app ...
```

- `-I...` cho phép `#include "bsp_rs485.h"` mà không cần ghi đường dẫn.
- `[env:native]` biên dịch `lib/pmems_core` + `test/` bằng gcc của máy tính để chạy unit test.
- Thư viện trong `lib/` được PlatformIO **tự động** phát hiện khi có file include header của nó.

### 4.2. `scripts/fpu.py`

```python
fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=%s" % abi]
env.Append(CCFLAGS=fpu_flags, ASFLAGS=fpu_flags, LINKFLAGS=fpu_flags)
```

Builder `stm32cube` chỉ thêm `-mthumb -mcpu=cortex-m4` (không có FPU). Script này thêm cờ FPU cho
**tất cả** (C, assembly, link) để mọi file dùng cùng một cách truyền số thực. Thiếu cờ này,
FreeRTOS `port.c` báo lỗi `vstmdbeq ... not supported`.

### 4.3. `scripts/freertos.py`

Builder `stm32cube` chỉ biên dịch HAL, không biên dịch FreeRTOS dù mã nguồn nằm sẵn trong gói
framework. Script này:
1. Tìm thư mục `framework-stm32cubef4/Middlewares/Third_Party/FreeRTOS/Source`.
2. Thêm đường dẫn header (`include/`, `CMSIS_RTOS_V2/`, `portable/GCC/ARM_CM4F/`).
3. Dùng `env.BuildSources()` biên dịch đúng các file cần: nhân (`tasks.c`, `queue.c`...),
   lớp bọc `cmsis_os2.c`, port `ARM_CM4F/port.c` và bộ cấp phát `heap_4.c`.

---

## 5. Thư mục `include/` – cấu hình toàn cục

### 5.1. `app_config.h` – "bảng thông số" của máy

Chia theo mục của spec. Các nhóm chính:

| Nhóm | Hằng số tiêu biểu | Ý nghĩa |
|---|---|---|
| §1.1 Băng tải | `ENC_PULSES_PER_MM = 10`, `CAN_PITCH_MM = 120` | 1 xung encoder = 0.1 mm; lon cách nhau 120 mm |
| §1.2 Tọa độ trạm | `ENC_POS_CAMERA 500` … `ENC_POS_END 20000` | Vị trí các trạm tính bằng xung từ S2 |
| | `STATION_IDX_CAMERA = 1` … `STATION_IDX_END = 6` | Số thứ tự trạm "kế tiếp" của một lon |
| | `TRACKING_TICK_HZ 5000`, `ORIENT_PRECLOSE_PULSES 300` | Tần số quét, đóng P3 sớm 30 mm |
| §2 Vision | `VISION_TIMEOUT_MS 80`, `VISION_MAX_CONSEC_TIMEOUT 3` | Chờ kết quả tối đa 80 ms; 3 lần liên tiếp → alarm |
| §3 Dán tem | `LABEL_T_P5_DOWN_MS 160`, `LABEL_T_PRESS_MS 210`… | Các mốc thời gian trong chu kỳ dán |
| §4 Động học | `M2_PULSES_PER_DEG`, `M3_VMAX_HZ`, `M6_SOFT_MAX_MM`… | Xung/đơn vị, tốc độ, gia tốc, giới hạn từng trục |
| §6 An toàn | `PURGE_TIME_MS 5000`, `CYL_GENERIC_TIMEOUT_MS 300` | Thời gian xả băng tải, timeout xi lanh |
| §7 Modbus | `HMI_BAUD 19200`, `VFD_REG_CMD 0x2000`… | Thông số truyền thông |
| §8 EEPROM | `EEPROM_I2C_ADDR 0xA0`, `EEPROM_PAGE_SIZE 32` | Địa chỉ, kích thước trang |

Ví dụ cách viết hằng số có công thức (dễ kiểm tra lại với spec):

```c
#define M2_PULSES_PER_DEG  (1600.0f / 360.0f)            /* 1600 xung/vòng → 4.444 xung/độ */
#define M2_ACCEL_HZ_S      (12000.0f / 60.0f * 1600.0f)  /* 12000 rpm/s → xung/s² */
```

### 5.2. `FreeRTOSConfig.h`

| Dòng | Giải thích |
|---|---|
| `configTICK_RATE_HZ 1000` | Nhịp hệ điều hành 1 ms → `vTaskDelay(1)` = 1 ms |
| `configMAX_PRIORITIES 56` | Bắt buộc = 56 khi dùng CMSIS-RTOS v2 |
| `configTOTAL_HEAP_SIZE 48*1024` | 48 KB RAM để tạo task/queue/semaphore |
| `configCHECK_FOR_STACK_OVERFLOW 2` | Phát hiện tràn stack → gọi hook trong `error_handler.c` |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5` | Ngắt mức 5..15 được gọi API `...FromISR` |
| `configASSERT(x)` | Điều kiện sai → `app_rtos_assert_failed()` → dừng an toàn |
| `vPortSVCHandler SVC_Handler` | Đổi tên hàm của FreeRTOS cho khớp tên trong bảng vector |
| `CMSIS_device_header "stm32f4xx.h"` | `cmsis_os2.c` cần biết header của chip |

### 5.3. `stm32f4xx_hal_conf.h`

Sao từ template của ST, chỉ chỉnh 2 chỗ:
- **Chỉ bật module HAL cần dùng** (GPIO, RCC, DMA, TIM, UART, I2C, SPI, IWDG, FLASH, PWR, CORTEX,
  EXTI). Module tắt sẽ không được biên dịch → firmware nhỏ hơn.
- `HSE_VALUE = 8000000` – tần số thạch anh ngoài (TODO(HW): kiểm tra board).

---

## 6. Thư viện lõi `lib/pmems_core/`

Đặc điểm chung: chỉ dùng `<stdint.h>`, `<string.h>`, `<math.h>`, không biết gì về chip.

### 6.1. `crc16` – mã kiểm tra lỗi

```c
uint16_t crc16_modbus_update(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;                                   // trộn byte vào CRC
        for (int i = 0; i < 8; i++) {                     // xử lý từng bit
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
```

- CRC = "chữ ký" 16 bit của gói tin. Bên gửi tính rồi gắn vào cuối; bên nhận tính lại, khác
  nhau → gói bị nhiễu → bỏ.
- Đa thức 0xA001, giá trị đầu 0xFFFF = chuẩn **Modbus**. Dùng cho Vision, Modbus RTU, Recipe.
- Kiểm chứng: chuỗi `"123456789"` cho ra `0x4B37` (test `test_crc16_known_vector`).

### 6.2. `can_record.h` + `can_fifo.c` – sổ theo dõi lon

**Bản ghi một lon** (theo spec §1.3, thêm 2 trường mở rộng):

```c
typedef struct {
    uint16_t    can_id;          // số thứ tự lon 1..65535
    uint32_t    enc_capture_s2;  // số đếm encoder lúc lon đi qua S2
    float       theta_degree;    // góc khoen do Vision trả về
    uint8_t     station_idx;     // trạm KẾ TIẾP lon sẽ tới (1..6)
    CanStatus_t status;          // trạng thái hiện tại
    uint32_t    timestamp_enter; // thời điểm qua S2 (tick ms)
    uint32_t    timestamp_trig;  // [mở rộng] thời điểm kích camera (tính timeout 80 ms)
    uint8_t     flags;           // [mở rộng] CAN_FLAG_HOLD, CAN_FLAG_VISION_TMO
} CanRecord_t;
```

**Trạng thái lon** (`CanStatus_t`) đi theo một chiều:

```
INIT → VISION_WAIT → VISION_OK → ORIENTED → LABELED → INSPECTED → PASSED
                └────────► lỗi (≥ 0x80): VISION_NG / VACUUM_MISS / LABEL_MISS → REJECTED
```

`can_status_is_error(s)` = `s >= 0x80`.

**Quãng đường lon đã đi** – một dòng nhưng rất quan trọng:

```c
static inline int32_t can_distance(const CanRecord_t *r, uint32_t enc_now)
{
    return (int32_t)(enc_now - r->enc_capture_s2);
}
```

Phép trừ **không dấu** rồi ép sang có dấu: kể cả khi bộ đếm 32 bit tràn từ `0xFFFFFFFF` về `0`,
kết quả vẫn đúng (test `test_can_distance_wraps`).

**FIFO vòng (ring buffer)** 32 phần tử:

```c
typedef struct { CanRecord_t buf[32]; uint8_t head; uint8_t count; } CanFifo_t;
```

- `head` = vị trí lon **cũ nhất** (gần cuối băng tải nhất); `count` = số lon đang trên băng tải.
- `push`: ghi vào ô `(head + count) % 32`. `pop`: lấy ô `head`, `head = (head+1) % 32`.
- `can_fifo_at(f, i)`: lon thứ i tính từ cũ nhất. `can_fifo_find(f, id)`: tìm theo mã lon.
- FIFO **không tự khóa**; người gọi (`tracking.c`) phải bọc vùng găng.

Vì sao FIFO? Lon trên băng tải không thể vượt nhau → lon vào trước luôn ra trước.

### 6.3. `vision_proto` – gói tin với PC Vision (spec §2.2)

**Gói MCU → PC (8 byte):** `55 AA 10 [ID thấp] [ID cao] [CRC thấp] [CRC cao] 0A`

**Gói PC → MCU (12 byte):** `AA 55 20 [ID 2B] [θ×100 2B] [chất lượng] [CRC 2B] 0D 0A`

Bộ giải mã là **máy trạng thái theo từng byte**, vì byte từ UART về rời rạc:

```c
bool vision_parser_feed(VisionParser_t *p, uint8_t byte, VisionResult_t *out)
{
    switch (p->len) {
    case 0: if (byte == 0xAA) lưu; return false;        // chờ byte đầu
    case 1: if (byte == 0x55) lưu; else resync; ...     // chờ byte thứ 2
    case 2: if (byte == 0x20) lưu; else resync; ...     // chờ mã lệnh
    }
    lưu byte; nếu chưa đủ 12 byte → return false;
    đủ 12 byte: kiểm tra 0D 0A ở cuối, kiểm tra CRC → điền *out, return true
}
```

- `resync()`: khi gặp byte sai, nếu chính byte đó là `0xAA` thì coi như bắt đầu gói mới
  → tự đồng bộ lại sau nhiễu mà không mất gói kế tiếp.
- `crc_errors`, `frame_errors`: đếm lỗi để hiển thị/gỡ lỗi (thanh ghi 30021).

### 6.4. `modbus_pdu` – Modbus dùng chung

Modbus có 2 lớp: **PDU** (mã hàm + dữ liệu, giống nhau mọi kiểu truyền) và **vỏ bọc**
(RTU: `[địa chỉ][PDU][CRC]`; TCP: `[MBAP 7 byte][PDU]`).

```c
typedef struct {
    uint16_t holding_count, input_count;
    uint8_t (*read_holding)(void *ctx, uint16_t addr, uint16_t *val);
    uint8_t (*write_holding)(void *ctx, uint16_t addr, uint16_t val);
    uint8_t (*read_input)(void *ctx, uint16_t addr, uint16_t *val);
    void *ctx;
} MbMap_t;
```

Thư viện **không chứa** dữ liệu thanh ghi, chỉ gọi các con trỏ hàm do ứng dụng cung cấp
(`modbus_regs.c`). Nhờ vậy cùng một code phục vụ được cả HMI (RTU) lẫn SCADA (TCP).

| Hàm | Vai trò |
|---|---|
| `mb_pdu_process` | Xử lý FC03 (đọc holding), FC04 (đọc input), FC06 (ghi 1), FC16 (ghi nhiều); sai → trả mã ngoại lệ (exception) |
| `mb_rtu_slave_process` | Kiểm CRC, kiểm địa chỉ slave, gọi PDU, gắn CRC trả lời; địa chỉ 0 (broadcast) không trả lời |
| `mb_tcp_process` | Kiểm MBAP, gọi PDU, chép Transaction ID vào gói trả lời |
| `mb_rtu_build_read / build_write_single` | Phía master (biến tần): tạo gói yêu cầu |
| `mb_rtu_parse_read_resp / parse_write_single_resp` | Phía master: kiểm tra và tách dữ liệu gói trả lời |

Mã ngoại lệ: `01` sai mã hàm, `02` sai địa chỉ, `03` sai giá trị, `04` thiết bị lỗi.

### 6.5. `pulse_ramp` – tăng/giảm tốc hình thang

Động cơ không thể nhảy từ đứng yên lên 48 000 xung/s (sẽ trượt bước). Phải tăng dần, chạy đều,
rồi giảm dần → biểu đồ tốc độ hình thang:

```
tốc độ │      ┌──────────┐ f_max
       │     /            \
       │    /              \
f_start│───/                \───
       └────────────────────────► thời gian
```

Công thức vật lý dùng: `v² = v₀² + 2·a·s`. Với mỗi bước (s = 1 xung):

```c
float ramp_step(RampState_t *r)
{
    r->done++;                                         // đã phát thêm 1 xung
    if (!r->continuous && r->done >= r->total) return 0.0f;   // đủ số xung → dừng

    // cần bao nhiêu xung để giảm từ tốc độ hiện tại về f_start?
    float decel_steps = (r->v2 - r->v2_start) / r->two_a;
    decel = (số xung còn lại) <= decel_steps;          // tới lúc phải giảm tốc chưa?

    if (decel)              r->v2 -= r->two_a;         // giảm tốc
    else if (v2 < v2_max)   r->v2 += r->two_a;         // tăng tốc (tới khi chạm f_max)
    return sqrtf(r->v2);                               // tần số cho xung kế tiếp
}
```

- Lưu `v²` thay vì `v` để mỗi bước chỉ cần một phép cộng + một `sqrtf` (FPU làm ~14 chu kỳ).
- `continuous` (total = 0): chạy mãi, dùng cho jog/homing; `ramp_request_stop()` để giảm tốc dừng.
- `ramp_finish_after(extra)`: "dừng sau extra xung nữa" – dùng khi gặp cảm biến khe tem.
- `ramp_profile_from_times()`: cho trước số xung và thời gian tăng tốc/chạy đều (vd M3 quay 60°
  trong 35/80/35 ms) → tính ra `f_max` và gia tốc. Test kiểm chứng tổng thời gian ≈ 150 ms.

### 6.6. `orient_math` – góc cần xoay

```c
total = θ_vision + offset_recipe/100 + offset_HMI/100;   // độ
total = chuẩn hóa về (-180, +180];                       // không bao giờ quay quá nửa vòng
m2_deg = total * scale_x1000 / 1000;                     // scale = D_lon / D_bánh_PU
pulses = round(m2_deg * 4.444);
```

Vì sao nhân `D_lon / D_PU`? Bánh PU Ø95 lăn trên thân lon Ø66: cùng một cung tiếp xúc,
lon quay `x` độ thì bánh chỉ cần quay `x × 66/95 = x × 0.695` độ.

### 6.7. `recipe_types` – công thức sản phẩm

- `CanRecipe_t` đúng **64 byte** (`#pragma pack(1)` để không có byte đệm), có `_Static_assert`
  để trình biên dịch báo lỗi nếu ai đó thêm trường làm sai kích thước.
- `recipe_seal()` tính CRC 62 byte đầu ghi vào 2 byte cuối; `recipe_valid()` kiểm tra lại
  → phát hiện EEPROM trống hoặc hỏng.
- `recipe_factory_default(id, m6_ref, out)`: 4 recipe mẫu theo spec §8.2; chiều cao M6 trong spec
  là tương đối so với lon 330 ml nên cộng thêm `m6_ref`.
- `recipe_check_ranges()`: kiểm tra mọi giá trị nằm trong phạm vi an toàn trước khi lưu.
- `SysConfig_t` (64 byte): magic `"PME1"`, recipe đang dùng, bộ đếm trọn đời, cấu hình IP.

### 6.8. `recipe_regs` – recipe ↔ thanh ghi Modbus

Chuyển 64 byte recipe thành 19 thanh ghi 16 bit (40021..40039) và ngược lại, để PC đọc/ghi qua
Ethernet. Tên 16 ký tự được ghép 2 ký tự/thanh ghi (byte cao trước). Khi chuyển ngược, hệ số
M2 được **tính lại từ đường kính** (`recipe_scale_from_diameter`), bỏ qua giá trị PC gửi lên.

---

## 7. `src/main.c` và `src/core/`

### 7.1. `main.c` – điểm vào

```c
int main(void)
{
    HAL_Init();               // (1) khởi tạo HAL, nhịp 1 ms trên TIM7
    system_clock_config();    // (2) 16 MHz → 168 MHz
    bsp_board_init();         // (3) chân GPIO về trạng thái an toàn
    osKernelInitialize();     // (4) chuẩn bị FreeRTOS
    app_create();             // (5) tạo task "init"
    osKernelStart();          // (6) giao CPU cho FreeRTOS - không bao giờ trả về
    error_handler();          // chỉ tới đây nếu (6) thất bại
}
```

Vì sao làm ít trong `main()`? Sau API FreeRTOS đầu tiên, các ngắt mức 5..15 bị chặn cho tới khi
scheduler chạy → `HAL_Delay()` (dựa vào ngắt TIM7 mức 15) sẽ treo. Nên mọi thứ cần chờ đợi
(EEPROM, khởi động W5500…) được làm trong task `init`.

### 7.2. `system_clock.c` – xung nhịp

```
HSE 8 MHz → /8 = 1 MHz → ×336 = 336 MHz → /2 = 168 MHz (SYSCLK)
                                         → /7 =  48 MHz (USB, chưa dùng)
SYSCLK → AHB /1 = 168 MHz → APB1 /4 = 42 MHz (timer ×2 = 84 MHz)
                          → APB2 /2 = 84 MHz (timer ×2 = 168 MHz)
```

`FLASH_LATENCY_5`: Flash chậm hơn CPU nên mỗi lần đọc phải chờ 5 chu kỳ.

### 7.3. `hal_timebase_tim7.c` – nhịp HAL

HAL cần biến `uwTick` tăng mỗi 1 ms. Mặc định dùng SysTick, nhưng SysTick đã thuộc FreeRTOS.
File này **ghi đè** 3 hàm `__weak` của HAL (`HAL_InitTick`, `HAL_SuspendTick`, `HAL_ResumeTick`)
và viết `TIM7_IRQHandler()` gọi `HAL_IncTick()` mỗi 1 ms.

### 7.4. `stm32f4xx_it.c` – mọi hàm ngắt

| Hàm ngắt | Chuyển cho | Tần suất |
|---|---|---|
| `HardFault_Handler` … | `error_handler()` | khi có lỗi |
| `EXTI2_IRQHandler` | `tracking_on_s2_isr(TIM2->CNT)` | mỗi lon |
| `TIM6_DAC_IRQHandler` | `bsp_tick_irq()` → `tracking_tick_isr()` | 5000 lần/s |
| `TIM1_UP_TIM10_IRQHandler` … (5 hàm) | `bsp_pulse_irq(AXIS_Mx)` | mỗi xung động cơ |
| `USART2/3`, `UART4_IRQHandler` | `bsp_rs485_irq_uart()` | khi IDLE / gửi xong / lỗi |
| `DMA1_StreamX_IRQHandler` (6 hàm) | `bsp_rs485_irq_dma_rx/tx()` | DMA quay vòng / xong |

Chi tiết đáng chú ý ở `EXTI2_IRQHandler`: dòng **đầu tiên** đọc `TIM2->CNT` – mỗi micro-giây
chậm trễ thì lon đã đi thêm một đoạn, nên phải chụp encoder trước mọi việc khác.

### 7.5. `crit.h` – vùng găng

```c
uint32_t s = crit_enter();   // BASEPRI = 5 → chặn ngắt mức 5..15
... sửa dữ liệu dùng chung ...
crit_exit(s);                // trả BASEPRI cũ
```

Ngắt phát xung (mức 2) **vẫn chạy** trong vùng găng → xung động cơ không bị méo. Dùng được trong
task, trong ngắt và trước khi scheduler chạy. Lồng nhau được vì lưu và trả lại giá trị cũ.

### 7.6. `error_handler.c`

`error_handler()`: tắt ngắt → KM1 = 0, phanh M6 = 0 (ghi `BSRR` trực tiếp) → 24 DO về ảnh an toàn
→ đứng chờ watchdog reset. Cũng chứa 3 hook FreeRTOS: assert sai, tràn stack, hết heap.

---

## 8. Lớp phần cứng `src/bsp/`

### 8.1. `board_pins.h` – bảng gán chân

Mỗi tín hiệu có 2 macro: `XXX_PORT` (GPIOA..E) và `XXX_PIN` (GPIO_PIN_0..15). Chân có nhãn
`[SPEC]` lấy từ spec; `[TODO(HW)]` là giả định cần kiểm tra. Ví dụ:

```c
#define CAM_TRIG_PORT     GPIOD      /* [SPEC] PD14 */
#define CAM_TRIG_PIN      GPIO_PIN_14
#define DI_LOAD_PORT      GPIOD      /* [TODO(HW)] chân LOAD của 74HCT165 */
```

### 8.2. `bsp_board.c` – GPIO cấp board

- `bsp_board_init()`: bật xung 5 port → cấu hình ngõ ra (đặt mức an toàn **trước** khi bật
  output) → ngõ vào (tín hiệu an toàn dùng PULLDOWN để đứt dây = "không an toàn") → chân S2
  dùng ngắt cạnh xuống, ưu tiên 5 (chưa bật, `tracking_init()` sẽ bật).
- `bsp_delay_us()`: chờ bằng bộ đếm chu kỳ CPU **DWT->CYCCNT** (168 lần đếm = 1 µs).
- `bsp_cam_trigger_pulse()`: xung 10 µs bằng `BSRR` (an toàn trong ngắt).
- `bsp_estop_ok()`, `bsp_km1_set()`, `bsp_home_active()`…: hàm 1 dòng gói `HAL_GPIO_ReadPin/WritePin`
  để lớp app không cần biết chân cụ thể.

### 8.3. `bsp_encoder.c` – encoder băng tải

TIM2 ở **chế độ encoder TI12**: phần cứng tự đếm lên/xuống theo 2 tín hiệu A/B lệch pha 90°.
CPU chỉ cần đọc `TIM2->CNT` (hàm inline `bsp_encoder_now()`). TIM2 là timer 32 bit → 429 km
băng tải mới tràn. `IC1Filter = 6` lọc gai nhiễu trên dây encoder.

### 8.4. `bsp_tick.c` – ngắt 5 kHz

TIM6 (basic timer): `PSC` chia 84 MHz → 1 MHz, `ARR = 199` → tràn mỗi 200 µs. Ngắt mức 5 gọi
hàm callback đã đăng ký (`tracking_tick_isr`). Dùng con trỏ hàm để bsp không phụ thuộc app.

### 8.5. `bsp_dio.c` – 32 DI / 24 DO

**Đọc (74HCT165):**
1. Xung `LOAD` thấp → 4 IC chụp 32 ngõ vào cùng lúc.
2. Lặp 40 lần: đọc chân `DATA` → xung `CLK` để dịch bit kế tiếp ra.
3. 4 byte đầu = 32 DI; byte thứ 5 = chân nối GND trên module → luôn `0x00`.
   Nếu đọc ra `0xFF` (chân DATA có pull-up, cáp tuột thì lơ lửng = 1) → **tuột cáp** → ALM_20.

**Ghi (74HCT595):**
1. `s_do_image`: 24 bit trong RAM, các module sửa bằng `bsp_dio_do_write()` (có vùng găng).
2. `bsp_dio_do_commit()`: dịch 24 bit ra `SER` + xung `SRCLK`, rồi xung `RCLK` để 24 ngõ ra
   đổi **đồng thời**.
3. `/OE` giữ mức 1 (ngõ ra tắt) cho tới lần commit đầu tiên → khi mới cấp điện không kích nhầm van.

`short_delay()` = 12 lệnh `NOP` (~70 ns) để xung đủ rộng cho IC 74HCT.

### 8.6. `bsp_rs485.c` – 3 cổng RS485

Cấu trúc dữ liệu:
- `PortHw_t s_hw[3]` (hằng): UART nào, luồng DMA nào, chân nào.
- `PortRt_t s_rt[3]` (RAM): Handle HAL (`huart`, `hdma_rx`, `hdma_tx`), bộ đệm, semaphore.

**Khởi tạo** `bsp_rs485_init()` – 6 bước: bật xung → chân AF → DMA nhận (CIRCULAR) →
DMA gửi (NORMAL) → UART (chú ý: có parity thì `WordLength = 9B`) → bật 3 ngắt mức 6 →
`start_rx()`.

**Nhận** – không xử lý từng byte:
```
HAL_UARTEx_ReceiveToIdle_DMA(&huart, rx_buf, 256)   // gọi 1 lần, chạy mãi
   byte về → DMA tự chép vào rx_buf
   đường im lặng (IDLE) → HAL_UARTEx_RxEventCallback(huart, size)
       size = vị trí DMA đang ghi tới; dữ liệu mới = rx_buf[rx_pos .. size)
       nếu size < rx_pos: DMA đã quay vòng → chia 2 khúc
       → gọi rt->cb(data, len, idle) của module ứng dụng (vision / hmi / vfd)
```

**Gửi** `bsp_rs485_send()`:
```
chép data → tx_buf → DIR = 1 → chờ 5 µs → HAL_UART_Transmit_DMA()
→ task NGỦ chờ semaphore tx_done
   (DMA gửi hết, byte cuối ra khỏi chân) → HAL_UART_TxCpltCallback()
       chờ 15 µs → DIR = 0 → xSemaphoreGiveFromISR(tx_done)
→ task thức dậy, trả về true
```

**Lỗi** `HAL_UART_ErrorCallback()`: nhiễu/tràn làm HAL dừng DMA nhận → gọi lại `start_rx()`
để cổng không bị "điếc".

### 8.7. `bsp_pulse.c` – phát xung động cơ

Mỗi trục dùng kênh 1 của một timer ở **PWM mode 2**, xung vào timer chia về 10.5 MHz:

```
tần số xung = 10 500 000 / (ARR + 1)      CCR1 = ARR/2 → xung vuông 50%
```

- **Preload** (`ARPE`, `OC1PE`): ghi ARR mới trong ngắt chỉ có hiệu lực từ chu kỳ sau → đổi tốc
  độ không làm méo xung đang phát.
- `bsp_pulse_start()`: đặt DIR → `ramp_init()` → nạp ARR/CCR1 → bật ngắt update → chạy.
- `bsp_pulse_irq()` (mỗi xung): `pos += dir` → `f = ramp_step()` → `f == 0` thì `hw_stop()`,
  ngược lại nạp ARR mới.
- `hw_stop()`: ép ngõ ra xuống 0 (`OCM_FORCE_INACTIVE`) **trước** khi dừng đếm → không có xung thừa.
- TIM1/TIM8 là advanced timer: phải bật `BDTR.MOE` mới ra xung.
- Ngắt mức 2, không gọi FreeRTOS; task biết đã xong qua `bsp_pulse_busy()`.

### 8.8. `bsp_eeprom.c` – AT24C64

- I2C1 400 kHz, chân **open-drain** (`GPIO_MODE_AF_OD`).
- `bsp_eeprom_read()`: `HAL_I2C_Mem_Read()` (địa chỉ ô nhớ 16 bit), không giới hạn độ dài.
- `bsp_eeprom_write()`: **chia theo trang 32 byte** (ghi vượt trang sẽ quay vòng đè dữ liệu),
  sau mỗi trang chờ chip ghi xong (~5 ms) bằng `HAL_I2C_IsDeviceReady()`.
- Mutex: nhiều task có thể dùng EEPROM cùng lúc.

### 8.9. `bsp_w5500.c` – Ethernet

- Giao dịch SPI: `[địa chỉ 16 bit][byte điều khiển (khối, đọc/ghi)][dữ liệu]`, CS = 0 trong suốt.
- `bsp_w5500_init()`: reset chân RST → đọc `VERSIONR` phải bằng `0x04` → ghi MAC, IP, mask, gateway.
- Socket TCP: `sock_listen()` (OPEN → LISTEN), `sock_recv()` (đọc con trỏ `Sn_RX_RD`, đọc bộ đệm,
  cập nhật con trỏ, lệnh `RECV`), `sock_send()` (tương tự với `Sn_TX_WR`, lệnh `SEND`).
- Thanh ghi 16 bit đang thay đổi phải đọc 2 lần tới khi khớp (`rd16_stable`), theo datasheet.
- MAC tạo từ mã định danh duy nhất (UID 96 bit) của chip STM32 → mỗi board một MAC khác nhau.

---

## 9. Lớp ứng dụng `src/app/`

### 9.1. `app_main.c` – khởi động và bảng task

```c
static const TaskDef_t k_tasks[] = {
    {io_task,             "io",      1024, osPriorityRealtime},
    {station_label_task,  "label",   1024, osPriorityHigh2},
    {station_orient_task, "orient",  1024, osPriorityHigh1},
    {station_feed_task,   "feed",     768, osPriorityHigh},
    {vision_task,         "vision",  1024, osPriorityAboveNormal},
    {machine_task,        "machine", 1280, osPriorityNormal1},
    {homing_task,         "homing",  1024, osPriorityNormal},
    {hmi_link_task,       "hmi",     1280, osPriorityNormal},
    {vfd_task,            "vfd",     1024, osPriorityBelowNormal},
    {net_link_task,       "net",     1536, osPriorityLow},
};
```

`init_task()` khởi tạo **theo thứ tự có chủ ý**:
1. `io_init()` – ngõ ra an toàn sớm nhất.
2. `recipe_init()` + `params_init()` – đọc EEPROM (cần HAL_Delay/vTaskDelay nên phải ở trong task).
3. `motion_init`, `bsp_encoder_init`, `machine_init`, `modbus_regs_init`, `vision_init`,
   `hmi_link_init`, `vfd_init`.
4. `tracking_init()` **cuối cùng** – bật ngắt S2 và TIM6 khi mọi queue đã sẵn sàng.
5. Tạo 10 task → `osThreadExit()` (task init tự xóa).

### 9.2. `io_map.h` – tên tín hiệu

```c
typedef enum { DI_S7_LANE1 = 0, ..., DI_P3_EXT = 8, ..., DI_SPARE_32 = 31 } DiBit_t;
typedef enum { DO_CYL_P1 = 0, ..., DO_ENA_M2 = 16, ..., DO_TOWER_GRN = 23 } DoBit_t;
```

Giá trị = `byte × 8 + bit`, khớp bảng spec §5. Hai macro đáng chú ý:
- `DI_INVERT_MASK`: đảo cực tính ngõ vào nếu module opto đảo (TODO(HW)).
- `DO_SAFE_IMAGE`: ảnh ngõ ra an toàn. Lưu ý: `DO_ENA_Mx = 1` nghĩa là **tắt** driver (spec §5.2),
  nên ảnh an toàn có các bit ENA = 1.

### 9.3. `io.c` – task I/O 1 ms

```c
for (;;) {
    bsp_dio_scan(&raw, &extra);                 // 1. đọc 40 bit
    if (extra == 0xFF) đếm lỗi; 3 lần liên tiếp → ALM_20, giữ ảnh DI cũ
    else s_di = raw ^ DI_INVERT_MASK;

    read_safety(ext, bus_ok);                   // 2. chuỗi an toàn: GPIO (PCB v2), byte LI mở
                                                //    rộng (PCB v1), hoặc bỏ qua (bản bàn thử)
    if (!estop_ok) { KM1 = 0; phanh M6; ALM_01 }   // phản ứng NGAY, không chờ FSM
    if (!door) ALM_02;   if (!air) ALM_03;

    check_driver_alarms(s_di);                  // 3. lỗi driver (chỉ khi đã cấp điện > 500 ms)
    if (DI_S14_TAPE_END) ALM_07;

    tracking_service_1ms();                     // 4. hẹn giờ thu xi lanh P8
    bsp_dio_do_commit();                        // 5. xuất 24 DO
    s_heartbeat++;                              // 6. nhịp tim cho watchdog
    vTaskDelayUntil(&last, 1 ms);
}
```

Các hàm tiện ích cho module khác: `di(DI_xxx)`, `do_set(DO_xxx, on)`, `do_commit_now()`
(xuất ngay, cho trạm cần thời gian chính xác), `io_wait_di(bit, level, timeout)` (chờ cảm biến
xi lanh, trả `false` khi quá thời gian).

Nguồn tín hiệu an toàn chọn bằng `SAFETY_SOURCE` (`app_config.h`). Với PCB v1, 4 tín hiệu an toàn
đi qua một IC 74HCT165 nối thêm vào cuối chuỗi (byte thứ 5); mất bus → tất cả bị coi là "không
an toàn". Bản `controller1_bench` bỏ qua an toàn nhưng `bsp_km1_set()` luôn ghi 0 (khóa KM1).

`ALM_14` (lỗi driver M6) được xử lý **ngay tại đây**: đóng phanh và cắt driver để trục nâng 15 kg
không bị rơi.

### 9.4. `alarm.c` – bảng alarm

- `s_mask`: 32 bit, bit n = alarm mã n đang bật. `alarm_raise()`/`alarm_clear()` dùng vùng găng
  → gọi được từ ngắt.
- `k_severity[]`: mức độ của từng mã:

| Mức | Hành vi FSM | Mã |
|---|---|---|
| `WARNING` | Chỉ hiển thị | 04, 06 |
| `DRAIN_STOP` | Ngừng cấp lon, chạy hết lon trên băng rồi dừng | 07 |
| `STOP` | Dừng có kiểm soát → ALARM_STOP | 03, 05, 08, 10–20 |
| `EMERGENCY` | → ESTOP, cắt KM1 | 01, 02 |

- `alarm_current()`: mã hiển thị trên HMI = mã có mức cao nhất (bằng nhau thì lấy mã nhỏ hơn).
- Cơ chế **chốt (latch)**: alarm giữ nguyên tới khi nhấn RESET. Nếu nguyên nhân vẫn còn, task io
  sẽ bật lại trong vòng 1 ms.

### 9.5. `tracking.c` – theo dõi lon (quan trọng nhất)

**Dữ liệu:** `s_fifo` (sổ lon), 3 queue tới các task (`s_q_trig`, `s_q_orient`, `s_q_label`),
bộ đếm sản lượng `s_cnt`, tốc độ băng tải `s_pulses_per_s`.

**`tracking_on_s2_isr(enc)` – lon mới (ngắt EXTI2):**
```c
if (!s_enabled) return;                               // chỉ nhận lon khi đang AUTO
if (lon trước cách < 600 xung (60 mm)) return;        // chống dội / nhiễu cảm biến
tạo CanRecord_t { can_id = s_next_id++, enc_capture_s2 = enc, station_idx = CAMERA, ... };
if (!can_fifo_push(...)) alarm_raise(ALM_18);         // > 32 lon: kẹt băng tải
s_cnt.fed++;
```

**`tracking_tick_isr()` – quét 5000 lần/s (ngắt TIM6):**
```c
enc = encoder hiện tại;  now = tick ms;
mỗi 100 ms: s_pulses_per_s = (enc - enc_100ms_trước) * 10;          // đo tốc độ
label_lead = s_pulses_per_s * 210 / 1000;   // lon đi được bao xa trong 210 ms

crit_enter();
for (mỗi lon trong FIFO) {
    vision_timeout_check();                   // quá 80 ms chưa có kết quả → VISION_NG
    if (lon đang bị P3 giữ) continue;         // vị trí "đóng băng"
    d = can_distance(lon, enc);
    switch (lon->station_idx) {               // chỉ xét trạm KẾ TIẾP của lon
      CAMERA : d >= 500             → ev_camera()
      ORIENT : d >= 3000 - 300      → ev_orient()
      LABEL  : d >= 6500 - lead     → ev_label()
      INSPECT: d >= 9500            → ev_inspect()
      REJECT : d >= 12500           → ev_reject()
    }
}
lon cũ nhất đã tới 20000 → pop khỏi FIFO, nếu PASSED thì s_cnt.ok++
crit_exit();
portYIELD_FROM_ISR(woken);  // nếu vừa đánh thức task ưu tiên cao → chuyển ngay
```

Các hàm sự kiện:

| Hàm | Việc làm |
|---|---|
| `ev_camera` | Xung CAM_TRIG 10 µs, trạng thái `VISION_WAIT`, ghi `timestamp_trig`, gửi mã lon vào queue cho task vision (để gửi gói 0x10) |
| `ev_orient` | Chỉ lon `VISION_OK`: gửi sự kiện cho task orient, đặt cờ `HOLD`. Lon lỗi thì bỏ qua trạm |
| `ev_label` | Chỉ lon `ORIENTED`: gửi sự kiện cho task label. Gửi **sớm 210 ms** vì chu kỳ dán cần 210 ms từ lúc mâm bắt đầu quay tới lúc ép tem |
| `ev_inspect` | Lon `LABELED`: đọc S5 → `INSPECTED` hoặc `LABEL_MISS` |
| `ev_code` | (camera 2, 1050 mm) Lon `INSPECTED`: xung CAM2_TRIG, `CODE_WAIT`, gửi gói 0x11; kết quả 0x21 → `CODE_OK` / `CODE_NG`, quá 150 ms → NG |
| `ev_reject` | Lon **không** `CODE_OK` (hoặc không `INSPECTED` nếu tắt camera 2) → bật P8, hẹn 80 ms thu về, `REJECTED`, `ng++`. Ngược lại `PASSED` |

Quy tắc an toàn chất lượng: **chỉ lon đã được S5 xác nhận có tem mới được đi qua**; mọi trường hợp
khác (lỗi Vision, hụt chân không, trạm bận, mất tem…) đều bị loại.

**Hàm cho task:** `tracking_get(id, &out)` (đọc bản sao), `tracking_edit(id, fn, ctx)` (sửa bản
ghi trong vùng găng bằng hàm callback), `tracking_clear()` (xóa sạch khi E-Stop/xả băng tải).

### 9.6. `vision.c` – PC Vision

```
ngắt UART4 → rx_cb() → xStreamBufferSendFromISR(s_rx, data)     (ngắt: chỉ chép byte)
task vision (lặp):
   1. lấy mã lon trong queue trigger → vision_build_trigger() → bsp_rs485_send()
   2. xStreamBufferReceive(..., chờ tối đa 2 ms)
   3. từng byte → vision_parser_feed() → đủ gói hợp lệ:
        tracking_edit(id, apply_result):
           chỉ nhận nếu lon còn VISION_WAIT (quá 80 ms rồi thì bỏ, đếm rx_late)
           quality == OK và |θ| ≤ 180° → theta_degree, VISION_OK
           ngược lại → VISION_NG
        tracking_vision_result_ok() → xóa bộ đếm timeout liên tiếp
```

### 9.7. `station_feed.c` – cấp lon P1/P2

Khi được bật và băng tải đã đi thêm 120 mm kể từ lần nhả trước và S0 thấy có lon chờ:
```
P2 ra (nêm chen giữa lon 1 và 2) → P1 về (lon 1 đi) → chờ S1 thấy lon
→ P1 ra (chặn lại) → P2 về (hàng lon dồn lên)
```
Mỗi bước `cyl()` chờ cảm biến từ của xi lanh, quá 300 ms → ALM_15. Khi tắt (trừ lúc E-Stop):
giữ P1 ra để chặn hàng lon.

### 9.8. `station_orient.c` – chỉnh góc

```
nhận sự kiện (can_id)
→ P3 ra, chờ DI_P3_EXT                   (chốt chặn)
→ chờ lon thực sự tới (d >= 3000), chờ thêm 20 ms cho lon đứng yên
→ P4 kẹp, chờ DI_P4_EXT
→ pulses = orient_m2_pulses(θ, offset recipe, offset HMI, hệ số D/95)
→ M2 quay, chờ xong (≤ 500 ms)
→ LUÔN nhả P4, rút P3 (kể cả khi bước trước lỗi)
→ status = ORIENTED (hoặc VISION_NG nếu lỗi)
→ release_hold(): enc_capture_s2 = enc_now - 3000   ← "đặt lại tọa độ"
```

Vì sao đặt lại tọa độ? Lon bị P3 giữ đứng yên trong khi băng tải vẫn chạy. Nếu không chỉnh,
hệ thống sẽ tưởng lon đã đi xa hơn thực tế và kích trạm dán tem sai lúc.

### 9.9. `station_label.c` – dán tem 6 đầu hút

**Đánh số đầu hút:** mâm có 6 đầu, mỗi lần quay 60°. `s_index` = số lần đã quay (mod 6).
```c
static inline uint8_t head_at(uint8_t k) { return (k + 6 - s_index) % 6; }
// k = vị trí trạm: 0 → 0° (cấp tem), 1 → 60° (kiểm tra), 3 → 180° (dán), 4 → 240°
```

**Trạng thái mỗi đầu:** `EMPTY → LOADED (vừa nhận tem) → OK / MISS (sau kiểm tra chân không) → EMPTY (đã dán)`.

**Quay M3 không bị trôi:** 60° = 100000/6 = 16666.67 xung, không chẵn. Thay vì quay 16667 xung mỗi
lần (sai 2 xung mỗi vòng, cộng dồn mãi), code tính **vị trí tuyệt đối** `round((s_index+1) × 100000 / 6)`
và quay tới đó; hết một vòng thì trừ 100000.

**`run_cycle()` – một chu kỳ (mốc thời gian tính từ lúc bắt đầu quay):**
```
t =   0..150 ms  M3 quay 60° (35 ms tăng tốc / 80 ms đều / 35 ms giảm tốc)
dwell (sau khi M3 dừng):
   đầu 0°   : bật hút; +30 ms bật P7 thổi hỗ trợ; +60 ms tắt P7 → LOADED
              M4 kéo tem; khi cảm biến khe tem → chạy thêm N xung (overrun) rồi dừng
              quá 300 ms không thấy khe → ALM_07 (hết/đứt tem)
   đầu 60°  : +vacuum_detect_ms đọc công tắc chân không → OK hoặc MISS (+ALM_06)
   đầu 180° (nếu có lon và đầu OK):
      t = 160 ms        P5 xuống; phải thấy DI_P5_DOWN trong 150 ms, không → ALM_08
      t = up - blow     tắt hút + bật thổi nhả tem
      t = 210 + dwell   P5 lên, tắt thổi; lon → LABELED
                        phải thấy DI_P5_UP trong 150 ms và trước 380 ms, không → ALM_08
   đầu 180° bị MISS: lon → VACUUM_MISS (sẽ bị loại), bỏ tem hỏng
kết thúc khi mọi việc xong (không chờ đủ 400 ms)
```
Vòng lặp chạy mỗi 1 ms (`vTaskDelayUntil`), so `t` với các mốc → mỗi hành động thực hiện đúng
một lần nhờ các cờ `p5_down_cmd`, `released`, `p5_up_cmd`…

**Mồi đầu hút (prime):** khi START, các đầu còn trống. `station_label_request_prime()` cho mâm chạy
tối đa 4 chu kỳ không có lon để nạp tem, tới khi đầu sắp tới vị trí 180° đã OK.

**Lon tới trễ:** sự kiện nằm trong queue quá 20 ms (trạm đang bận) → không kịp dán → `LABEL_MISS`.

### 9.10. `motion.c` – lớp trục

Bảng `k_axis[]` chứa thông số từng trục (lấy từ `app_config.h`):

```c
[AXIS_M6] = {"M6", "mm", 320 xung/mm, vmax, accel, tốc độ homing, chiều homing = lên,
             có home, có giới hạn mềm 0..98 mm, vị trí tại home = 98 mm},
```

| Hàm | Việc làm |
|---|---|
| `motion_enable_all(on)` | Bật/tắt driver (ENA tích cực mức 0, SON_M3 tích cực mức 1); bật thì chờ 100 ms rồi mới nhả phanh M6 |
| `motion_move_abs(ax, pos, speed)` | Kiểm giới hạn mềm → đổi ra xung → `bsp_pulse_start()` |
| `motion_move_rel`, `motion_jog`, `motion_stop`, `motion_abort_all` | Di chuyển tương đối, chạy tay, dừng giảm tốc, dừng ngay |
| `motion_wait(ax, timeout)` | Chờ trục xong (ngủ 1 ms mỗi vòng); quá giờ thì dừng ngay và trả `false` |
| `motion_home_axis(ax)` | Về gốc 3 pha: tìm cảm biến nhanh → lùi ra khỏi cảm biến → tiến lại chậm (×0.1) để lặp lại chính xác → gán `home_pos` |

Trục M6 có 2 cảm biến (M6a/M6b) nhưng chỉ một chuỗi xung → dừng khi **bất kỳ** cảm biến nào tác động.

### 9.11. `homing.c` – về gốc toàn máy (spec §6.2)

```
1. Nhả P4, nâng P5 (BẮT BUỘC trước tiên), chờ cảm biến xác nhận
2. Xả băng tải: biến tần 10 Hz trong 5 s, dừng, xóa FIFO
3. Bật driver
4. Home M6 (lên trên cùng) → M3 (+12.5° tới trạm 0, đặt lại đầu hút) → M4 → M5 → M2 = 0
5. Đưa M6 tới chiều cao của recipe
→ machine_homing_done(ok)
```
Chạy trong task riêng vì mất nhiều giây; task machine vẫn chạy song song (giám sát E-Stop). Mỗi
bước kiểm tra `machine_abort_requested()` để dừng nếu có E-Stop/alarm.

### 9.12. `machine.c` – máy trạng thái (FSM)

**Các trạng thái và mã gửi lên HMI (30001):**

| Trạng thái | HMI | Ý nghĩa |
|---|---|---|
| `MS_POWER_ON` | 0 | Mới bật nguồn, chờ 300 ms |
| `MS_ESTOP` | 1 | Dừng khẩn cấp |
| `MS_HOMING_REQUIRED` | 8 | Cần về gốc |
| `MS_HOMING_EXEC` | 2 | Đang về gốc |
| `MS_READY` | 3 | Sẵn sàng |
| `MS_AUTO_RUN` | 4 | Đang sản xuất |
| `MS_AUTO_PAUSE` | 6 | Tạm dừng |
| `MS_ALARM_STOP` | 5 | Dừng do lỗi |
| `MS_MANUAL_JOG` | 7 | Chạy tay |

**Hai hàm chính:**
- `enter(new_state)`: hành động khi **vào** trạng thái. Ví dụ `MS_ESTOP`: cắt KM1, đóng phanh,
  tắt driver, ngõ ra an toàn, xóa FIFO, đánh dấu cần homing. `MS_AUTO_RUN`: chạy biến tần theo
  tốc độ, bật tracking, bật cấp lon, bật motor cuộn đế tem, mồi đầu hút (nếu từ READY).
- `fsm_step(cmd)`: mỗi 10 ms quyết định có chuyển trạng thái không.

**Luật chuyển trạng thái chính:**
```
(mọi trạng thái) ALM_01 hoặc (cửa mở khi đang chuyển động) → ESTOP
POWER_ON  → ESTOP (nếu chuỗi an toàn chưa OK) | HOMING_REQUIRED
ESTOP     → HOMING_REQUIRED   khi: E-Stop nhả + cửa đóng + nhấn SAFETY_RESET
HOMING_REQUIRED + lệnh 5      → HOMING_EXEC
HOMING_EXEC → READY (thành công) | ALARM_STOP (thất bại / alarm)
READY + lệnh 1 (START)        → AUTO_RUN   (nếu không alarm, cửa đóng, M6 đã tới chiều cao)
READY + lệnh 6                → MANUAL_JOG
AUTO_RUN: alarm STOP → ALARM_STOP; lệnh 2 → READY; lệnh 3 → PAUSE;
          S0 hết lon > 2 s → PAUSE (tự động); ALM_07 → ngừng cấp, hết lon → ALARM_STOP
AUTO_PAUSE → AUTO_RUN (lệnh 1, hoặc S0 có lon lại nếu pause tự động)
ALARM_STOP + lệnh 4 (RESET) → xóa alarm → READY (hoặc phải homing lại nếu lỗi trục)
```

**Việc phụ mỗi 10 ms:** `update_lamps()` (đèn tháp + còi), `recipe_service()` (ghi EEPROM khi
không chạy), `watchdog_kick()` (chỉ "cho ăn" watchdog khi task io còn chạy – nếu task io treo,
MCU sẽ bị reset).

**Lệnh từ HMI/PC** đi qua queue (`machine_post_cmd`) → chỉ task machine được đổi trạng thái,
tránh 2 task cùng sửa.

### 9.13. `recipe.c` và `params.c`

- `recipe_init()`: đọc SysConfig (0x0000) và recipe đang dùng (0x0040 + (id-1)×64). Hỏng/trống →
  ghi giá trị mặc định.
- `recipe_get_active(&r)`: trả **bản sao** (có mutex) để task khác dùng an toàn.
- `recipe_select(id)`, `recipe_load(id)`, `recipe_store(r)`: chọn / đọc / lưu ngay.
- `recipe_update_active()` + `recipe_service()`: sửa trong RAM, task machine ghi EEPROM sau
  (chỉ khi máy không chạy, tránh ghi EEPROM lúc đang sản xuất).
- `params.c`: tham số chỉnh nhanh từ HMI không lưu EEPROM (tốc độ, offset góc ±18°, trục/chiều jog).

### 9.14. `modbus_regs.c` – bản đồ thanh ghi

Ba hàm callback cho `MbMap_t`:
- `rd_holding(addr)`: 40001..40011 (lệnh, recipe, tốc độ, offset…) + vùng recipe editor.
- `wr_holding(addr, val)`: kiểm tra phạm vi rồi gọi module tương ứng (`machine_post_cmd`,
  `recipe_select`, `params_mut()`…). Sai → mã ngoại lệ.
- `rd_input(addr)`: **tính trực tiếp** từ trạng thái hiện tại (FSM, alarm, bộ đếm, DI/DO, biến tần…).

**Xi lanh bằng tay (40012):** ghi → `machine_manual_outputs(mask)`; chỉ nhận ở MANUAL_JOG, có khóa
liên động P5/M3. Task machine áp dụng ra ngõ ra (một nơi ghi duy nhất); thoát chạy tay → tắt hết.

**Recipe editor (40021..40041):** có bộ đệm `s_rcp_buf` + mutex. PC ghi một lần FC16 cả dữ liệu
lẫn lệnh (lệnh ở thanh ghi cuối nên được thực thi sau cùng); `rcp_execute()` chạy **đồng bộ** nên
khi PC nhận được phản hồi Modbus thì kết quả (40041) đã sẵn sàng.

### 9.15. `hmi_link.c`, `vfd.c`, `net_link.c` – truyền thông

| Module | Vai trò | Cơ chế |
|---|---|---|
| `hmi_link` | Slave RTU (ID 1, 19200 8N1) | Ngắt gom byte vào `s_rx`; IDLE → `vTaskNotifyGiveFromISR` đánh thức task → `mb_rtu_slave_process` → gửi trả lời |
| `vfd` | Master RTU (ID 2, 9600 8E1) | Mỗi 100 ms: ghi tần số/lệnh chạy **khi thay đổi**, đọc 5 thanh ghi 0x2100..0x2104; 10 lần lỗi liên tiếp → ALM_19; mã lỗi biến tần ≠ 0 → ALM_17 |
| `net_link` | Server TCP cổng 502 | Mỗi 5 ms duyệt 4 socket: CLOSED → listen; ESTABLISHED → nhận, `mb_tcp_process`, gửi; CLOSE_WAIT → ngắt kết nối |

`vfd_freq_for_speed()`: lon/phút → mm/s → Hz theo `BELT_MM_S_PER_HZ` (cần đo thực tế).

---

## 10. Các luồng hoạt động từ đầu tới cuối

### 10.1. Bật nguồn tới khi sẵn sàng

```
Reset → startup.s → main(): HAL_Init, 168 MHz, GPIO an toàn → osKernelStart
 → task init: DO an toàn, đọc recipe, khởi tạo ngoại vi, bật tracking, tạo 10 task
 → machine: POWER_ON (300 ms)
      chuỗi an toàn OK → bật KM1 → HOMING_REQUIRED
 → người vận hành nhấn HOME (40001 = 5) → HOMING_EXEC
      homing task: P4/P5 an toàn → xả băng tải 5 s → bật driver → home M6, M3, M4, M5 → M6 tới chiều cao recipe
 → READY
```

### 10.2. Một lon từ đầu tới cuối (ở 240 mm/s)

| Thời điểm | Vị trí | Ai xử lý | Sự kiện |
|---|---|---|---|
| 0 ms | S2 | ngắt EXTI2 | Tạo bản ghi, id = 57 |
| 208 ms | +50 mm | ngắt TIM6 `ev_camera` | Kích camera, `VISION_WAIT`, queue → task vision gửi gói 0x10 |
| ~240 ms | | task vision | Nhận gói 0x20: θ = +35.42°, `VISION_OK` |
| 1125 ms | +270 mm | ngắt TIM6 `ev_orient` | Queue → task orient; P3 ra, cờ HOLD |
| 1250 ms | +300 mm | task orient | Lon chạm P3 → kẹp P4 → M2 quay 109 xung → nhả → `ORIENTED`, đặt lại tọa độ |
| … | +600 mm (≈ 650 − 50) | ngắt TIM6 `ev_label` | Queue → task label (sớm 210 ms) |
| +150 ms | | task label | M3 quay xong 60° |
| +160 ms | | task label | P5 xuống |
| +210 ms | +650 mm | | Tem ép lên nắp lon |
| +250 ms | | | P5 lên, `LABELED` |
| … | +950 mm | ngắt TIM6 `ev_inspect` | S5 thấy tem → `INSPECTED` |
| … | +1250 mm | ngắt TIM6 `ev_reject` | Không loại → `PASSED` |
| … | +2000 mm | ngắt TIM6 | Xóa khỏi FIFO, `ok++` |

*(Thời gian sau trạm chỉnh góc phụ thuộc lon bị giữ bao lâu.)*

### 10.3. Lon bị lỗi Vision (timeout)

```
ev_camera (VISION_WAIT) → 80 ms không có kết quả → vision_timeout_check: VISION_NG + đếm
 → ev_orient: không phải VISION_OK → bỏ qua (P3/P4/M2 không chạy)
 → ev_label : lỗi → bỏ qua (không hạ P5)
 → ev_reject: không INSPECTED → P8 đẩy ra máng, ng++
 (3 lon timeout liên tiếp → ALM_05 → ALARM_STOP)
```

### 10.4. Nhấn E-Stop khi đang chạy

```
≤ 1 ms : task io thấy E-Stop → KM1 = 0, phanh M6, ALM_01                (phản ứng tức thì)
≤ 10 ms: task machine → enter(ESTOP): dừng biến tần, tắt driver, DO an toàn, xóa FIFO, còi kêu
Nhả E-Stop + đóng cửa + nhấn SAFETY_RESET → HOMING_REQUIRED → phải homing lại
```

### 10.5. PC đổi đường kính lon (Ethernet)

```
PC Python: plc.save_recipe(r, activate=True)
 → FC16 ghi 40021..40040 (dữ liệu + lệnh 3)
 → task net → mb_tcp_process → wr_holding → rcp_write → rcp_execute:
      recipe_from_regs (tính lại hệ số M2) → kiểm phạm vi → máy có đang chạy không?
      → recipe_store (ghi EEPROM) → recipe_select → machine_request_recipe_apply
 → PC đọc 40041 = 1 (OK)
 → task machine (READY): M6 chạy tới chiều cao mới; START bị giữ tới khi M6 tới nơi
```

---

## 11. Kiểm thử `test/` và công cụ PC `tools/pc_config/`

### 11.1. Unit test C (`pio test -e native`)

`test/test_core/test_main.c` có 16 test cho `lib/pmems_core`, chạy trên máy tính:

| Test | Kiểm tra |
|---|---|
| `test_crc16_known_vector` | CRC đúng chuẩn Modbus |
| `test_vision_*` (3) | Đóng gói, giải gói kèm nhiễu, phát hiện CRC sai |
| `test_fifo_*`, `test_can_distance_wraps` | FIFO quay vòng, khoảng cách khi encoder tràn |
| `test_modbus_*` (3) | RTU đọc/ghi, ngoại lệ, sai slave, sai CRC, TCP ghi nhiều |
| `test_ramp_*` (3) | Đúng số xung, đạt tốc độ tối đa, dừng giảm tốc, M3 quay 60° đúng ~150 ms |
| `test_orient_math` | Chuẩn hóa góc, số xung M2 |
| `test_recipe_*` (3) | Mặc định, CRC, chuyển đổi thanh ghi (khớp với test Python) |

### 11.2. `tools/pc_config/` (Python)

| File | Vai trò |
|---|---|
| `pmems_config/registers.py` | Địa chỉ thanh ghi, `Recipe` (đơn vị mm/độ) ↔ thanh ghi, giải mã `MachineStatus` |
| `pmems_config/client.py` | `PmemsClient`: `read_status`, `read_recipe`, `save_recipe`, `select_recipe`, `send_command` |
| `pmems_config/__main__.py` | Dòng lệnh: `python -m pmems_config --host ... status / recipe-list / recipe-set ...` |
| `tests/` | 10 test (chạy không cần máy thật, dùng bộ điều khiển giả lập) |

---

## 12. Hướng dẫn sửa / mở rộng thường gặp

### Đổi một chân MCU
Sửa `src/bsp/board_pins.h`. Nếu là chân ngoại vi (UART, timer…) phải kiểm tra bảng Alternate
Function trong datasheet và sửa số `GPIO_AFx_...` tương ứng trong bảng `s_hw[]` của driver.

### Đổi một thông số quy trình
Sửa `include/app_config.h` (vd `VISION_TIMEOUT_MS`, `ENC_POS_LABEL`). Không cần sửa logic.

### Thêm một mã alarm
1. Thêm hằng vào `AlarmCode_t` (`alarm.h`).
2. Thêm mức độ vào `k_severity[]` (`alarm.c`).
3. Gọi `alarm_raise(ALM_xx)` nơi phát hiện lỗi.
4. Thêm tên vào `ALARM_NAMES` trong `tools/pc_config/pmems_config/registers.py`.

### Thêm một thanh ghi Modbus
- Holding (ghi được): thêm `case` trong `rd_holding()` và `wr_holding()` (`modbus_regs.c`).
- Input (chỉ đọc): thêm `case` trong `rd_input()`.
- Cập nhật chú thích bản đồ ở đầu `modbus_regs.h` và phía Python nếu PC cần dùng.

### Thêm một task FreeRTOS
1. Viết `void my_task(void *arg) { for (;;) { ...; vTaskDelay(...); } }`.
2. Thêm dòng vào `k_tasks[]` trong `app_main.c` (tên, stack theo byte, ưu tiên).
3. Nếu dùng chung dữ liệu với ngắt: `volatile` + `crit_enter/exit`. Giữa các task: mutex/queue.

### Thêm một xi lanh / cảm biến mới trên LI32/LO24
Dùng bit dự phòng (`DI_SPARE_32`, `DO_SPARE_16`): đổi tên trong `io_map.h`, rồi dùng
`di(...)` / `do_set(...)`.

### Gỡ lỗi khi máy dừng ở `error_handler()`
Đặt breakpoint trong `error_handler()`, xem **Call Stack**. Tràn stack: tăng `stack_bytes`
trong `k_tasks[]`. Hết heap: tăng `configTOTAL_HEAP_SIZE`.

---

## 13. Bảng tra cứu nhanh

### Mức ưu tiên

| Loại | Mức | Thành phần |
|---|---|---|
| Ngắt NVIC | 2 | Phát xung M2..M6 |
| | 5 | S2 (EXTI2), tracking (TIM6) |
| | 6 | UART, DMA |
| | 15 | HAL tick (TIM7), FreeRTOS |
| Task | Realtime | io |
| | High2 / High1 / High | label / orient / feed |
| | AboveNormal | vision |
| | Normal1 / Normal | machine / homing, hmi |
| | BelowNormal / Low | vfd / net |

### Timer

| Timer | Dùng cho | File |
|---|---|---|
| TIM1, TIM8, TIM3, TIM4, TIM9 | Phát xung M2, M3, M4, M5, M6 | `bsp_pulse.c` |
| TIM2 | Encoder băng tải | `bsp_encoder.c` |
| TIM6 | Quét tracking 5 kHz | `bsp_tick.c` |
| TIM7 | Nhịp 1 ms của HAL | `hal_timebase_tim7.c` |
| SysTick | Nhịp 1 ms của FreeRTOS | `cmsis_os2.c` (FreeRTOS) |

### Queue / đồng bộ giữa ngắt và task

| Đối tượng | Từ → tới | Nội dung |
|---|---|---|
| `s_q_trig` | ngắt TIM6 → task vision | Mã lon cần gửi gói trigger |
| `s_q_orient` | ngắt TIM6 → task orient | `StationEvent_t {can_id, tick}` |
| `s_q_label` | ngắt TIM6 → task label | `StationEvent_t {can_id, tick}` |
| `s_rx` (StreamBuffer) | ngắt UART4 → task vision | Byte từ PC Vision |
| `tx_done` (semaphore) | ngắt UART → task gửi | Báo gửi xong |
| notification | ngắt UART → task hmi / vfd | Báo nhận xong một gói |
| `s_cmdq` | task hmi/net → task machine | Lệnh START/STOP/… |
