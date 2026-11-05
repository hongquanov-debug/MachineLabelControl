# Ghi chú đối chiếu Spec REV 3.2 ↔ Firmware

Tài liệu này ghi lại (A) các điểm **mâu thuẫn / thiếu** trong `FIRMWARE_PROCESS_SPEC.md` và cách
firmware đang xử lý, (B) các **giả định phần cứng** cần kiểm tra với *Hardware Spec V3.2* trước khi
nạp lên máy thật. Tìm nhanh trong code bằng từ khóa `TODO(HW)`.

---

## A. Mâu thuẫn / điểm chưa rõ trong spec

| # | Mục spec | Vấn đề | Cách firmware xử lý |
|---|---|---|---|
| A1 | §8.1 `CanRecipe_t` | Tổng các trường = 1+16+20+22+2 = **61 byte**, không phải 64 như mô tả. | `reserved[25]` để struct đúng 64 byte, có `_Static_assert`. CRC tính trên 62 byte đầu (`lib/pmems_core/include/recipe_types.h`). |
| A2 | §4 vs §3.2 | M3 ghi `Vmax = 50 kHz`, nhưng index 60° = 16 667 xung trong 35/80/35 ms cần đỉnh **~145 kHz**. (Ngoài ra 3000 rpm × 10 000 ppr = 500 kHz chứ không phải 50 kHz.) | `M3_VMAX_HZ = 150 kHz` (`include/app_config.h`). Cần xác nhận lại hộp số điện tử (electronic gear) của driver Servo M3, hoặc kéo dài thời gian index. |
| A3 | §8.2 | Recipe 2 có `M6 = -23.2 mm` nhưng `m6_height_pos_x10` là `uint16_t` và giới hạn mềm M6 là 0..98 mm. | Hiểu các giá trị M6 trong §8.2 là **tương đối so với lon 330 ml chuẩn**. Vị trí tuyệt đối = `M6_REF_POS_330ML_X10` (mặc định 30.0 mm, **cần hiệu chuẩn**) + chênh lệch. |
| A4 | §1.3 `CanStatus_t` | Trộn cờ bit (0x01..0x40) với mã lỗi 0x80..0x82 (trùng bit 7) → không dùng được như bitmask. | Dùng như **trạng thái tuần tự** (một giá trị tại một thời điểm); `can_status_is_error()` = `status >= 0x80`. Thêm 2 trường mở rộng `timestamp_trig`, `flags` (giữ nguyên các trường gốc). |
| A5 | §1.1 vs §3.2 | Năng suất tối đa 156 lon/phút = 385 ms/lon, nhưng chu kỳ dán thiết kế 400 ms. | Chu kỳ dán kết thúc ngay khi P5 đã lên (DI_P5_UP) và các tác vụ dwell xong (~300 ms), không chờ đủ 400 ms. Lon đến khi trạm còn bận quá `LABEL_MAX_LATENESS_MS` → `CAN_ERROR_LABEL_MISS`. |
| A6 | §7.1 `30001` | Chỉ có 6 mã trạng thái, FSM §6.1 có 9 trạng thái. | Thêm mã 6 = Paused, 7 = Manual Jog, 8 = Homing Required. |
| A7 | §6.3 | Không có mã lỗi cho xi lanh P1..P4/P8, homing thất bại, lỗi biến tần, tràn FIFO. | Thêm `ALM_15..ALM_19` (`src/app/alarm.h`). |
| A8 | §7.1 `40001` | Không có lệnh vào/ra chế độ chạy tay (FSM có `STATE_MANUAL_JOG`). | Mở rộng: 6 = JOG_ENTER, 7 = JOG_EXIT; `40010` = trục, `40011` = chiều (-1/0/+1). |
| A9 | §7.1 `30004..` | Không nói thứ tự word của giá trị 32-bit. | **High word trước** (30004 = 16 bit cao). |
| A10 | §7.1 `30011` | ISE30A nối vào MCU như công tắc số → không đo được áp suất liên tục. | Trả `50` (≥ 0.5 MPa) khi đủ áp, `0` khi thiếu áp. |
| A11 | §2.2 | Không ghi thứ tự byte của CRC16. | Theo chuẩn Modbus: **byte thấp trước**. |
| A12 | §1.2 | Cảm biến S5 (trạm 4) không có trong bảng DI §5.1. | Đọc trực tiếp GPIO (`S5_PORT/S5_PIN`). |
| A13 | §6 | E-Stop, cửa bảo vệ, áp suất khí, nút SAFETY_RESET, contactor KM1, phanh M6, còi không có trong bảng I/O. | Định nghĩa là GPIO trực tiếp của MCU trong `src/bsp/board_pins.h`. |
| A14 | — | Trình tự cơ cấu cấp lon P1/P2 (escapement) chưa mô tả. | `station_feed.c`: P2 chen → P1 mở → chờ S1 → P1 đóng → P2 về, mỗi 120 mm hành trình băng tải và khi S0 có lon. |
| A15 | §3.1 | Trạm 240° "thổi khí vệ sinh" nhưng không có van riêng trong bảng DO. | Chỉ tắt hút chân không đầu ở 240°. Cần bổ sung van nếu muốn thổi vệ sinh. |
| A16 | §1.2 | Lon bị P3 chặn lại tại trạm chỉnh góc nên vị trí theo encoder không còn đúng. | Đánh dấu `CAN_FLAG_HOLD` trong lúc giữ; khi nhả, gán lại tọa độ lon = 300 mm (re-anchor). Lưu ý cơ khí: lon phía sau có thể dồn vào lon đang bị giữ. |
| A17 | §7.2 | Không có tỉ lệ tốc độ băng tải ↔ tần số biến tần. | `BELT_MM_S_PER_HZ = 6.24` (50 Hz ↔ 312 mm/s) – **cần đo thực tế**. |
| A18 | §6.3 ALM_02 | "Cửa mở trong lúc chạy" – không rõ khi máy đứng yên. | Cửa mở khi đang có chuyển động → `STATE_ESTOP`. Khi đứng yên: chỉ cấm START/HOME, tự xóa khi đóng cửa. |

## B. Giả định phần cứng cần kiểm tra (TODO(HW))

| Hạng mục | Giá trị đang dùng | File |
|---|---|---|
| Thạch anh HSE | 8 MHz | `include/stm32f4xx_hal_conf.h` |
| Cổng của S2 (EXTI2) | PE2, cạnh xuống | `src/bsp/board_pins.h`, `include/app_config.h` |
| USART2 / USART3 TX-RX, chân DIR RS485_1/2 | PD5/PD6 + PD4, PD8/PD9 + PD7 | `board_pins.h` |
| Bus LI32 (74HCT165) | PD10 LD, PD11 CLK, PD13 DATA (bit-bang) | `board_pins.h` |
| Bus LO24 (74HCT595) | PE7 SER, PE8 SRCLK, PE10 RCLK, PE11 /OE | `board_pins.h` |
| Chân DIR các trục M2..M6 | PE12, PE13, PE14, PE15, PE6 | `board_pins.h` |
| W5500 CS / RST / INT | PB12 / PB1 / PB0 | `board_pins.h` |
| E-Stop, Door, Air, Reset, KM1, Brake M6, Buzzer, LED | PC0..PC5, PB7, PB6 | `board_pins.h` |
| Cực tính ngõ vào DI (opto) | không đảo (`DI_INVERT_MASK = 0`) | `src/app/io_map.h` |
| Cực tính home sensor, S5 | mức thấp = tác động | `board_pins.h` |
| Chiều đếm encoder | IC1 rising | `src/bsp/bsp_encoder.c` |
| Chiều homing từng trục | M6 lên (+), M4 (+), M3/M5 (−) | `src/app/motion.c` |
| Chiều quay M2 so với góc θ | dương = DIR mức 1 | `src/app/station_orient.c` |
| Khoảng chống dội S2, thời gian P8, timeout xi lanh | 60 mm, 80 ms, 300 ms | `include/app_config.h` |
| Cảm biến nhiệt độ tủ điện (ALM_04) | chưa có | `src/app/machine.c` `cabinet_temp_c()` |
