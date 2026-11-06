# Giao thức PC Vision ↔ MCU (RS485_3) – 2 camera

Mở rộng Mục 2 của spec REV 3.2 cho **2 camera** nối vào cùng một PC Vision:

| Camera | Vị trí | Việc | Gói kích (MCU → PC) | Gói kết quả (PC → MCU) | Hạn trả lời |
|---|---|---|---|---|---|
| **Camera 1** | +50 mm | Đo góc lệch khoen | `0x10` | `0x20` | 80 ms sau `CAM_TRIG` (PD14) |
| **Camera 2** | +1050 mm (`ENC_POS_CODE_CAM`) | Kiểm tra & đọc mã DataMatrix trên tem | `0x11` | `0x21` | 150 ms sau `CAM2_TRIG` (PD15) |

Cổng: UART4, 115200 bps, 8N1, bán song công. Số nhiều byte: **Little-Endian**.
CRC16 Modbus (khởi tạo 0xFFFF, đa thức 0xA001), **byte thấp trước**.
Thư viện Python có sẵn: [`tools/pc_config/pmems_config/vision_link.py`](../tools/pc_config/pmems_config/vision_link.py).

---

## 1. Phân công

```
                    MCU (thời gian thực)                       PC Vision (xử lý ảnh)
 lon tới +50 mm  ── CAM_TRIG (xung cứng) ───────────────────►  camera 1 chụp
                 ── gói 0x10 [CAN_ID] ─────────────────────►  gắn ảnh với CAN_ID
                                                               đo góc θ
                 ◄─ gói 0x20 [CAN_ID, θ×100, QUALITY] ──────  (≤ 80 ms)
 lon tới +1050 mm ─ CAM2_TRIG (xung cứng) ──────────────────►  camera 2 chụp
                 ── gói 0x11 [CAN_ID] ─────────────────────►  giải mã DataMatrix,
                                                               kiểm tra nội dung, lưu dữ liệu
                 ◄─ gói 0x21 [CAN_ID, GRADE, LEN, QUALITY] ─  (≤ 150 ms)
 lon tới +1250 mm: P8 loại nếu chưa có kết quả OK
```

- **MCU** chỉ lo thời điểm: kích camera, chờ kết quả có giới hạn, loại lon NG, báo alarm.
  MCU **không** nhận chuỗi dữ liệu DataMatrix.
- **PC** lo nội dung: giải mã, kiểm tra định dạng / đúng sản phẩm / trùng mã / chất lượng in,
  **lưu chuỗi dữ liệu kèm CAN_ID, thời gian, recipe** để truy xuất nguồn gốc.
  Đổi quy tắc kiểm tra chỉ cần sửa phần mềm PC, không phải nạp lại firmware.
- Camera 2 **chỉ được kích cho lon còn tốt** (đã có góc, đã dán, S5 thấy tem). Lon đã lỗi thì
  không chụp vì đằng nào cũng bị loại.

---

## 2. Gói kích (MCU → PC) – 8 byte

| Byte | Trường | Giá trị |
|:---:|---|---|
| 0–1 | Header | `55 AA` |
| 2 | CMD | `10` = camera 1, `11` = camera 2 |
| 3–4 | CAN_ID | uint16 LE |
| 5–6 | CRC16 | của byte 0..4 |
| 7 | Tail | `0A` |

Ví dụ (sinh từ code firmware): camera 2, lon 300 → `55 AA 11 2C 01 84 D1 0A`.

PC nên dùng **thời điểm xung cứng** (CAM_TRIG / CAM2_TRIG) để chụp, và dùng gói kích để biết
ảnh vừa chụp thuộc lon nào. Gói kích có thể đến sau ảnh vài ms.

## 3. Gói kết quả (PC → MCU) – 12 byte

| Byte | Trường | Camera 1 (`0x20`) | Camera 2 (`0x21`) |
|:---:|---|---|---|
| 0–1 | Header | `AA 55` | `AA 55` |
| 2 | CMD | `20` | `21` |
| 3–4 | CAN_ID | uint16 LE | uint16 LE |
| 5 | | θ×100 byte thấp | **GRADE** chất lượng in: 4=A 3=B 2=C 1=D 0=F, `FF` = không đo. Phần mềm mẫu gửi grade **độ tương phản (Symbol Contrast)**, một tham số của ISO/IEC 15415 |
| 6 | | θ×100 byte cao | **DATA_LEN**: số ký tự giải mã được (để thống kê) |
| 7 | QUALITY_FLAG | `00` OK, `01` không thấy khoen, `02` nắp lỗi, `03` sai loại | xem bảng dưới |
| 8–9 | CRC16 | của byte 0..7 | của byte 0..7 |
| 10–11 | Tail | `0D 0A` | `0D 0A` |

QUALITY_FLAG của camera 2:

| Mã | Tên | Ý nghĩa | Lon |
|:---:|---|---|---|
| `00` | OK | Có mã, đọc được, nội dung hợp lệ | **Đạt** |
| `10` | NO_CODE | Không thấy tem / không thấy mã | Loại |
| `11` | UNREADABLE | Thấy mã nhưng không giải được | Loại |
| `12` | INVALID | Đọc được nhưng sai định dạng / sai sản phẩm | Loại |
| `13` | DUPLICATE | Đọc được nhưng mã đã xuất hiện trước đó | Loại |
| `14` | LOW_GRADE | Đọc được nhưng chất lượng in dưới ngưỡng | Loại |

Ví dụ (sinh từ code firmware): lon 300, trùng mã, grade B, 42 ký tự → `AA 55 21 2C 01 03 2A 13 F7 2F 0D 0A`.

---

## 4. Phía MCU xử lý thế nào

| Tình huống | Kết quả |
|---|---|
| `0x21` QUALITY = `00` trong 150 ms | Lon đạt (`CAN_STATUS_CODE_OK`), đi qua P8 |
| `0x21` QUALITY ≠ `00` | Lon `CAN_ERROR_CODE_NG`, P8 loại |
| Không có `0x21` sau 150 ms | Lon NG (timeout), P8 loại |
| Kết quả đến muộn / CAN_ID không còn chờ | Bỏ qua (đếm ở thanh ghi 30022) |
| 3 lon liên tiếp timeout | **ALM_09** CODE_CAM_TIMEOUT → dừng máy |
| 5 lon liên tiếp NG | **ALM_21** CODE_NG_REPEATED → dừng máy (kiểm tra máy in mã / cuộn tem) |

Thông số trong `include/app_config.h`: `ENC_POS_CODE_CAM`, `CODE_TIMEOUT_MS`,
`CODE_MAX_CONSEC_TIMEOUT`, `CODE_MAX_CONSEC_NG`, `CODE_CHECK_ENABLE` (tắt camera 2),
`LABEL_S5_CHECK_ENABLE` (tắt S5 nếu camera 2 thay thế cảm biến S5).

Thanh ghi Modbus (cho giao diện): 30025–26 số mã OK, 30027–28 số mã NG,
30029 = kết quả gần nhất (byte thấp QUALITY, byte cao GRADE).

**Thời gian:** từ camera 2 (1050 mm) tới P8 (1250 mm) là 200 mm, tức ~640 ms ở tốc độ tối đa
312 mm/s. Hạn 150 ms để lại dư thời gian. Nếu dời camera 2 phải giữ điều kiện
S5 < camera 2 < P8 (firmware có kiểm tra lúc biên dịch).

---

## 5. Phần mềm PC cho camera 2 (camera công nghiệp + thuật toán)

Camera 2 là camera công nghiệp thường: PC nhận ảnh rồi **tự tìm và giải mã** DataMatrix.
Thư viện mẫu có sẵn trong `tools/pc_config/pmems_config/`:

| Module | Việc |
|---|---|
| `code_reader.py` | Giải mã bằng **zxing-cpp**: tìm mã, lấy chuỗi thô GS1, 4 góc, đo **độ tương phản** → grade A–F. Có ROI. Bỏ qua "mã" nhỏ hơn `min_symbol_px` không giải được (nhiễu). |
| `gs1.py` | Tách chuỗi GS1 theo AI (01 GTIN + kiểm tra số kiểm tra, 21 serial, 17 hạn dùng, 10 lô…). |
| `code_check.py` | Quy tắc → QUALITY_FLAG: NO_CODE → UNREADABLE → INVALID (không phải GS1, sai GTIN theo recipe, thiếu AI, độ dài, hết hạn, nhiều mã) → LOW_GRADE → DUPLICATE (serial đã dùng, lưu SQLite) → OK. |
| `camera2.py` | Ghép tất cả: `process(can_id, frame)` → gói `0x21` + kết quả; ghi **nhật ký truy xuất** SQLite (CAN_ID, chuỗi, GTIN, serial, lô, hạn, grade, thời gian, recipe). |
| `vision_link.py` | Đọc gói kích 0x10/0x11, tạo gói 0x20/0x21. |

```python
from pmems_config.camera2 import Camera2Station, TraceLog
from pmems_config.code_check import CodeChecker, CodeRules, DuplicateStore
from pmems_config.code_reader import CodeReader

station = Camera2Station(CodeReader(roi=(360, 200, 760, 740)),
                         CodeChecker(CodeRules(gtin="08935049501237"), DuplicateStore("serials.db")),
                         TraceLog("trace.db"), recipe_id=1)
frame_bytes, v = station.process(can_id, frame)   # frame: numpy ảnh xám từ SDK camera
serial_queue.put(frame_bytes)                     # một luồng duy nhất ghi ra COM
print(v.quality.name, v.hri, v.grade_letter, v.reason)
```

**Kết quả đo** (ảnh tổng hợp 1280×960, mã xoay ngẫu nhiên, nhiễu, trên máy phát triển):

| Cách đọc | Đọc đạt | Thời gian TB | Lâu nhất |
|---|---|---|---|
| Toàn khung | 98/100 | 39 ms | 46 ms |
| ROI 760×740 | 95/100 (một số tem lấn ra ngoài ROI) | 17 ms | 25 ms |

→ Dư nhiều so với hạn 150 ms. **ROI phải bao trọn vùng tem có thể xuất hiện.** Độ tin cậy thật
phải đo lại bằng ảnh chụp trên máy (ánh sáng, độ nét, độ phản quang của tem).

**Lưu ý:**
- Grade gửi đi là grade **độ tương phản** (tương phản = sáng − tối trong vùng mã, A ≥ 70 %,
  B ≥ 55 %, C ≥ 40 %, D ≥ 20 %). Đây không phải grade tổng ISO 15415; muốn có grade chuẩn cần
  thiết bị kiểm định (verifier) đã hiệu chuẩn. Grade tương phản đủ để phát hiện mực in mờ / nhòe.
- Đèn chiếu: tem/nắp lon kim loại dễ chói; nên dùng đèn vòm (dome) hoặc đèn chiếu nghiêng và
  phơi sáng ngắn (lon chạy 312 mm/s, phơi 80 µs ≈ nhòe 0.025 mm).
- Serial chỉ được đánh dấu "đã dùng" khi lon **đạt**; lon bị loại không chiếm serial.
- Phạm vi chống trùng: gọi `DuplicateStore.clear()` khi đổi ca/ngày tùy quy định.
- Chỉ **một luồng** được ghi ra cổng COM (RS485 bán song công): gom gói của 2 camera vào một hàng đợi.
