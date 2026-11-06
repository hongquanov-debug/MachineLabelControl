# MachineLabelControl – PMeMS Controller 1

Firmware bộ điều khiển trung tâm **dây chuyền chỉnh góc & dán tem nắp lon tự động** (Nam Viet F&B).

| | |
|---|---|
| MCU | STM32F407VET6 – 168 MHz, 512 KB Flash, 192 KB RAM |
| SDK | PlatformIO · `framework = stm32cube` (STM32Cube **HAL**) |
| RTOS | FreeRTOS 10.3.1 + CMSIS-RTOS v2 (lấy từ gói `framework-stm32cubef4`) |
| Đặc tả | [`docs/FIRMWARE_PROCESS_SPEC.md`](docs/FIRMWARE_PROCESS_SPEC.md) REV 3.2 – *single source of truth* |
| Ghi chú spec | [`docs/SPEC_NOTES.md`](docs/SPEC_NOTES.md) – mâu thuẫn trong spec & chân I/O cần xác nhận |
| Người mới | [`docs/HAL_GUIDE.md`](docs/HAL_GUIDE.md) – hướng dẫn đọc code, khái niệm HAL & FreeRTOS |
| Giải thích code | [`docs/CODE_WALKTHROUGH.md`](docs/CODE_WALKTHROUGH.md) – cấu trúc thư mục, giải thích từng module & luồng hoạt động |
| Giao diện PC | [`docs/ui/`](docs/ui/README.md) – bản mẫu HTML 1024×768 cho phần mềm PyQt6 |

> ⚠️ Các chân không có trong spec quy trình được gán tạm và đánh dấu `TODO(HW)`.
> **Phải đối chiếu với Hardware Spec V3.2 trước khi nạp lên máy thật** (xem `docs/SPEC_NOTES.md` mục B).

---

## 1. Build / Nạp / Test

```bash
pio run -e controller1
```

```bash
pio run -e controller1 -t upload
```

```bash
pio test -e native
```

- `controller1`: firmware cho STM32F407 (nạp bằng ST-Link).
  FPU Cortex-M4F được bật bởi `scripts/fpu.py` (builder `stm32cube` mặc định không bật FPU,
  khi đó FreeRTOS `port.c` báo lỗi `vstmdbeq ... not supported`). Sau khi đổi cờ build nên chạy
  `pio run -t clean` một lần.
- `controller1_bench`: bản **chỉ để thử board trên bàn** – bỏ qua chuỗi an toàn nhưng khóa KM1
  (không có nguồn động lực). Không nạp bản này lên máy thật.
- Nguồn tín hiệu an toàn (PCB v1 qua LI mở rộng / PCB v2 qua GPIO): `SAFETY_SOURCE` trong
  `include/app_config.h`, xem `docs/SPEC_NOTES.md` mục A20.
- `native`: unit test trên máy tính cho thư viện lõi `lib/pmems_core` (Unity).

## 2. Cấu trúc dự án

```
├── platformio.ini            # env controller1 (STM32) + native (unit test)
├── scripts/fpu.py            # bật FPU (-mfpu=fpv4-sp-d16 -mfloat-abi=hard) cho mọi thành phần
├── scripts/freertos.py       # build FreeRTOS + CMSIS-RTOS v2 từ framework-stm32cubef4
├── include/
│   ├── app_config.h          # TẤT CẢ hằng số quy trình (tọa độ trạm, timing, động học...)
│   ├── FreeRTOSConfig.h      # cấu hình kernel + chính sách ưu tiên ngắt
│   └── stm32f4xx_hal_conf.h  # module HAL được bật, HSE
├── lib/pmems_core/           # Logic THUẦN (không phụ thuộc phần cứng) → test được trên PC
│   ├── crc16                 # CRC16 Modbus
│   ├── vision_proto          # Giao thức Vision (Mục 2.2): build/parse + resync
│   ├── can_fifo              # CanRecord_t + FIFO 32 lon (Mục 1.3)
│   ├── modbus_pdu            # Modbus slave (RTU + TCP) & master RTU
│   ├── pulse_ramp            # Profile hình thang theo từng xung
│   ├── orient_math           # Tính góc bù M2
│   └── recipe_types          # Recipe 64 byte + SysConfig + CRC (Mục 8)
├── src/
│   ├── main.c
│   ├── core/                 # clock 168 MHz, vector ngắt, HAL timebase TIM7, error handler
│   ├── bsp/                  # Driver phần cứng (HAL)
│   │   ├── board_pins.h      # Bảng gán chân
│   │   ├── bsp_encoder       # TIM2 encoder 32-bit
│   │   ├── bsp_tick          # TIM6 5 kHz quét tracking
│   │   ├── bsp_dio           # LI32 (74HCT165) / LO24 (74HCT595)
│   │   ├── bsp_rs485         # 3 cổng RS485: DMA circular + IDLE, điều khiển DIR
│   │   ├── bsp_pulse         # Phát xung 5 trục TIM1/8/3/4/9
│   │   ├── bsp_eeprom        # AT24C64 I2C1
│   │   ├── bsp_w5500         # Ethernet W5500 SPI2
│   │   └── bsp_board         # Safety chain, home sensor, CAM_TRIG, delay µs
│   └── app/                  # Logic quy trình
│       ├── io_map.h          # Tên 32 DI / 24 DO (Mục 5)
│       ├── io                # Task I/O 1 ms, giám sát an toàn
│       ├── tracking          # Theo dõi lon theo encoder (Mục 1)
│       ├── vision            # Liên kết PC Vision (Mục 2)
│       ├── station_feed      # Cấp lon P1/P2
│       ├── station_orient    # Trạm 2: P3/P4/M2
│       ├── station_label     # Trạm 3: mâm 6 đầu M3/P5/chân không (Mục 3)
│       ├── motion            # Trục M2..M6, đơn vị kỹ thuật, homing (Mục 4)
│       ├── homing            # Trình tự homing & purge băng tải (Mục 6.2)
│       ├── machine           # FSM hệ thống (Mục 6.1)
│       ├── alarm             # Ma trận Alarm (Mục 6.3)
│       ├── recipe, params    # Recipe EEPROM & tham số vận hành
│       ├── modbus_regs       # Bản đồ thanh ghi chung HMI/SCADA (Mục 7)
│       ├── hmi_link          # Modbus RTU slave – HMI Kinco
│       ├── vfd               # Modbus RTU master – biến tần M1
│       ├── net_link          # Modbus TCP server – W5500
│       └── app_main          # Khởi tạo & tạo task
├── test/test_core/           # Unit test (Unity)
├── tools/pc_config/          # Thư viện + CLI Python cấu hình máy qua Ethernet (PC Vision)
└── docs/
```

Quy tắc phụ thuộc: `app → bsp → HAL`, `app/bsp → lib/pmems_core`. Thư viện `pmems_core` **không**
được include HAL/FreeRTOS để luôn test được trên PC.

## 3. Kiến trúc thời gian thực

### Task (CMSIS-RTOS v2)

| Task | Ưu tiên | Chu kỳ / kích hoạt | Vai trò |
|---|---|---|---|
| `io` | Realtime | 1 ms | Quét LI32, xuất LO24, chuỗi an toàn, lỗi driver |
| `label` | High2 | Queue / lon | Index M3 60°, P5, chân không, kéo tem M4 |
| `orient` | High1 | Queue / lon | P3 → P4 → M2 quay θ → nhả |
| `feed` | High | Mỗi 120 mm băng tải | Cơ cấu nhả lon P1/P2 |
| `vision` | AboveNormal | Stream RS485_3 | Gửi trigger, nhận kết quả, ghi θ vào record |
| `machine` | Normal1 | 10 ms | FSM, đèn tháp, còi, lưu recipe, watchdog |
| `homing` | Normal | Notify | Trình tự homing (Mục 6.2) |
| `hmi` | Normal | IDLE RS485_1 | Modbus RTU slave ID 1 |
| `vfd` | BelowNormal | 100 ms | Modbus RTU master tới biến tần |
| `net` | Low | 5 ms | Modbus TCP port 502, 4 kết nối |

### Ưu tiên ngắt (NVIC 4 bit)

| Ưu tiên | Ngắt | Ghi chú |
|---|---|---|
| 2 | TIM1/8/3/4/9 update (phát xung) | Không gọi API FreeRTOS, không bị critical section che |
| 5 | EXTI2 (S2), TIM6 (tracking 5 kHz) | = `configMAX_SYSCALL_INTERRUPT_PRIORITY` |
| 6 | USART2/3, UART4, DMA1 | |
| 15 | PendSV, SysTick (FreeRTOS), TIM7 (HAL tick) | |

### Luồng xử lý một lon

```
S2 (EXTI2) ── chụp TIM2->CNT, cấp can_id, push FIFO
   │ +500 xung   CAM_TRIG 10 µs, (gửi gói 0x10), chờ Vision ≤ 80 ms
   │             └─ quá 80 ms → NG_VISION, 3 lần liên tiếp → ALM_05
   │ +2700 xung  → queue orient (chỉ lon VISION_OK): P3, kẹp P4, M2 quay θ
   │ +6500 − v·210ms → queue label: index M3, P5 hạ dán đúng lúc lon tới
   │ +9500 xung  đọc S5 → INSPECTED / LABEL_MISS
   │ +12500 xung lon không INSPECTED → P8 đẩy loại
   └ +20000 xung xóa khỏi FIFO, đếm OK
```

## 4. Bản đồ Modbus & cấu hình từ PC

Xem đầu file [`src/app/modbus_regs.h`](src/app/modbus_regs.h). HMI (RTU) và PC/SCADA (TCP) dùng chung
một bản đồ thanh ghi.

Phân chia kết nối với PC Vision:
- **RS485_3**: chỉ dùng cho kết quả góc lệch từng lon (thời gian thực).
- **Ethernet (Modbus TCP)**: cấu hình recipe (đường kính, chiều cao, offset…), lệnh máy, trạng thái.
  Vùng "recipe editor" 40021..40041 – xem [`tools/pc_config/README.md`](tools/pc_config/README.md).

## 5. Việc cần làm trước khi chạy thử trên máy

1. Xác nhận toàn bộ mục `TODO(HW)` (chân I/O, cực tính, chiều quay) – `docs/SPEC_NOTES.md`.
2. Hiệu chuẩn `M6_REF_POS_330ML_X10`, `BELT_MM_S_PER_HZ`, `ORIENT_PRECLOSE_PULSES`.
3. Xác nhận cấu hình driver Servo M3 cho tần số xung index (~145 kHz) – mục A2.
4. Chạy thử từng trạm ở chế độ JOG trước khi bật AUTO.
