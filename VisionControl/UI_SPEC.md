# VisionControl – Đặc tả xử lý giao diện (UI_SPEC)

> Tài liệu tham chiếu cho người và **AI viết code Python** của phần mềm PC
> (PyQt6) điều khiển dây chuyền "PMeMS Controller 1".
> Khi sửa/ thêm code, **giữ đúng các quy tắc trong tài liệu này**; nếu cần đổi
> quy tắc thì cập nhật tài liệu này trong cùng commit.
>
> Liên quan: `../docs/FIRMWARE_PROCESS_SPEC.md` (quy trình máy),
> `../docs/VISION_PROTOCOL.md` (khung RS485), `pmems_config/README.md`
> (bản đồ thanh ghi Modbus), `CLAUDE.md` (cách merge thuật toán ảnh).

---

## 1. Phạm vi

| Có trong VisionControl | Không có (project khác, merge sau) |
|---|---|
| Giao diện 6 trang (1024×768, cảm ứng) | Thuật toán định góc tab (camera 1) |
| Modbus TCP tới STM32 (lệnh, trạng thái, recipe) | Thuật toán đọc DataMatrix (camera 2) |
| RS485_3: nhận trigger 0x10/0x11, gửi kết quả 0x20/0x21 | Driver/SDK camera công nghiệp |
| Khung pipeline camera + hợp đồng (interface) | |
| Lịch sử SQLite, nhật ký, báo cáo, lưu ảnh NG | |
| Chế độ giả lập `--sim` (MCU + camera + thuật toán giả) | |

Điểm merge duy nhất cho phần ảnh: `visioncontrol/vision/camera_source.py`,
`angle_detector.py`, `code_reader.py` (xem §9).

---

## 2. Cấu trúc thư mục

```
VisionControl/
├── main.py                     # điểm vào: python main.py [--sim] [--config f] [--fullscreen]
├── requirements.txt            # PyQt6, pymodbus, pyserial, numpy
├── UI_SPEC.md  CLAUDE.md  README.md
├── config/settings.json        # tạo khi bấm "Lưu cấu hình" (không commit)
├── data/                       # history.db, ng/<ngày>/*.png (không commit)
├── pmems_config/               # thư viện Modbus TCP (dùng được độc lập, có CLI)
│   ├── client.py               # PmemsClient: connect, read_status, send_command, recipe...
│   └── registers.py            # địa chỉ thanh ghi, MachineStatus, Recipe, MachineCmd, ALARM_NAMES
├── tests/                      # python -m unittest discover -s tests -t .
└── visioncontrol/
    ├── app.py                  # create(): QApplication + QSS + AppController + MainWindow
    ├── settings.py             # dataclass Settings, load()/save() JSON
    ├── controller.py           # AppController – đối tượng DUY NHẤT mà UI gọi
    ├── core/
    │   ├── models.py           # VisionRecord, LogEntry, overlay (OvCircle/OvLine/OvPolygon/OvText)
    │   ├── vision_protocol.py  # CRC16, VisionQ, CodeQ, TriggerParser, ResultParser, result_cam1/2
    │   ├── history.py          # HistoryDB (SQLite): events, production, vision
    │   ├── events.py           # EventDetector: so sánh 2 MachineStatus -> LogEntry
    │   └── can_tracker.py      # CanTracker: vị trí lon trên băng tải (hiển thị)
    ├── vision/
    │   ├── interfaces.py       # HỢP ĐỒNG: FrameSource, AngleDetector, CodeReader
    │   ├── camera_source.py    # MERGE: ExternalCameraSource
    │   ├── angle_detector.py   # MERGE: TabAngleDetector
    │   ├── code_reader.py      # MERGE: DataMatrixReader
    │   ├── sim.py              # bản giả lập của 3 interface
    │   └── __init__.py         # make_source / make_angle_detector / make_code_reader
    ├── workers/
    │   ├── plc_worker.py       # QThread: poll Modbus + hàng đợi job
    │   ├── vision_link.py      # SerialLink (pyserial) / SimLink
    │   └── camera_pipeline.py  # 1 thread / camera: grab -> thuật toán -> gửi MCU -> VisionRecord
    ├── sim/sim_mcu.py          # STM32 giả lập (cùng API PmemsClient + vai trò RS485)
    └── ui/
        ├── theme.py            # bảng màu C + QSS
        ├── widgets.py          # Card, KpiCard, Stat, Led, LedLabel, Segmented, ImageView, ConveyorView, BarChart
        ├── common.py           # STATE_PILL, ENABLED, ALARM_TEXT, confirm(), error()
        ├── main_window.py      # header, thanh nav, footer, QStackedWidget
        └── pages/              # dashboard, camera, monitor, config, report, log
```

---

## 3. Kiến trúc & luồng dữ liệu

```
           ┌──────────────── GUI thread (Qt event loop) ─────────────────┐
  Pages ──method──► AppController ──signals──► Pages                       │
           │            │   ▲                                              │
           │  submit()  │   │ status / job_done (queued signal)            │
           └────────────┼───┼──────────────────────────────────────────────┘
                        ▼   │
              PlcWorker (QThread) ── Modbus TCP ──► STM32 (40001.., 30001..)
                                                       │ RS485_3
   SerialLink thread ◄── trigger 0x10/0x11 ────────────┘
        │  on_trigger(cam, can_id, t_ms)  (chỉ enqueue)
        ▼
   CameraPipeline cam1 / cam2 (thread riêng)
        grab() -> detect()/read() -> link.write(0x20/0x21)  ──► STM32  (gửi MCU TRƯỚC)
        -> VisionRecord -> _sig_result (queued) -> AppController._on_result (GUI thread)
```

### 3.1 Quy tắc luồng (bắt buộc)

1. **Chỉ GUI thread** được chạm widget. Thread khác giao tiếp qua `pyqtSignal`
   (Qt tự xếp hàng sang GUI thread) – ví dụ `_sig_result`, `_sig_trigger`, `_sig_link`.
2. **Trang (page) không bao giờ** gọi Modbus, serial, camera hay thuật toán
   trực tiếp. Mọi thao tác đi qua phương thức của `AppController`.
3. Lệnh Modbus **không chặn**: `AppController.command()/set_speed()/...` gọi
   `PlcWorker.submit()` và trả về **job id** (int). Kết quả về qua
   `job_done(job_id, result, error_text)`. Trang nào cần kết quả thì lưu
   `self._jobs[job_id] = "loại"` và lọc trong slot `job_done`.
4. Đường thời gian thực (trigger → kết quả về MCU) **không đi qua GUI thread**:
   link thread chỉ `put_nowait` vào queue của pipeline; pipeline gửi kết quả
   về MCU **trước** rồi mới phát VisionRecord cho UI. UI chậm/đơ không được
   làm lon bị loại.
5. Vẽ lại giới hạn: ảnh camera ≤ 10 Hz (timer 100 ms + cờ `_dirty`), băng tải
   10 Hz, trạng thái theo chu kỳ poll (mặc định 250 ms). Không vẽ mỗi khi có sự kiện.
6. `setStyleSheet()` rất tốn (re-polish): chỉ gọi khi giá trị thay đổi – dùng
   `widgets.set_css(w, css)`. Màu theo trạng thái ưu tiên dùng property + QSS
   (`set_kind(w, "ok"|"err"|...)`).

### 3.2 AppController – API cho UI

| Signal | Kiểu | Khi nào |
|---|---|---|
| `status_changed` | `MachineStatus` | mỗi lần poll Modbus thành công |
| `plc_connection` | `(bool, str)` | đổi trạng thái kết nối Modbus |
| `link_connection` | `(bool, str)` | mở/lỗi cổng RS485 |
| `vision_result` | `VisionRecord` | mỗi lon xử lý xong (cam1 hoặc cam2) |
| `log_added` | `LogEntry` | có dòng nhật ký mới |
| `job_done` | `(int, object, str)` | job Modbus xong (err = "" nếu OK) |
| `settings_changed` | – | sau `save_settings()` |
| `recipe_changed` | – | trang Cấu hình đã chọn/kích hoạt recipe → header đọc lại |

| Phương thức | Thanh ghi | Trả về |
|---|---|---|
| `command(MachineCmd)` | 40001 | job id |
| `set_speed(lon/phút)` | 40003 | job id |
| `set_theta_offset(độ)` | 40004 (×100) | job id |
| `jog(axis, dir)` | 40010/40011 | job id |
| `set_output(name, on)` | 40012 bit | job id |
| `read_recipe(id)` / `save_recipe(r, activate)` / `select_recipe(id)` / `active_recipe_id()` | 40002, 40021..40041 | job id → `Recipe` / `int` |
| `save_settings()` | file JSON | – |
| `sim_set_safety(key, val)` | chỉ `--sim` | – |

Thuộc tính đọc được (chỉ đọc từ UI): `status`, `cfg`, `history`, `tracker`,
`belt_mm`, `log` (deque mới nhất trước), `stats[1|2]` (`ok, ng, late,
duplicate, avg_ms`), `last_record[1|2]`, `last_ng[1|2]`, `recent` (deque dict
`id, theta, cam1, cam2`), `link.crc_errors`, `sim`.

### 3.3 Dữ liệu chính

- `MachineStatus` (`pmems_config/registers.py`): `state`, `state_name`, `alarm_mask`,
  `fed, ok, ng, code_ok, code_ng, code_last_quality`, `speed_cans_min`, `cans_on_belt`,
  `di, do` (bitmask), `m6_height_mm`, `vfd_freq_hz, vfd_current_a`, an toàn
  `estop_ok, door_closed, air_ok_sw, reset_pressed, io_bus_ok, safety_bench_mode, safety_ext_raw`.
- `VisionRecord` (`core/models.py`): `camera, can_id, quality, quality_name, ok,
  elapsed_ms, late, theta_deg, data, grade, reason, image (numpy), overlay, ts`.
- `LogEntry`: `level` (`err|warn|info`), `category` (`alarm|event|vision|comm`),
  `source, message, code, ts`.

---

## 4. Khung cửa sổ (`ui/main_window.py`)

Kích thước cố định 1024×768 (hoặc full screen theo `settings.fullscreen`).

| Vùng | Cao/rộng | Nội dung | Nguồn dữ liệu |
|---|---|---|---|
| Header | 52 px | Pill trạng thái máy, recipe đang dùng, tốc độ, chip an toàn, 4 LED (PLC, RS485, Cam1, Cam2), nút người dùng, đồng hồ | `status_changed`, `plc_connection`, `link_connection`, `recipe_changed` |
| Nav trái | 92 px | 6 nút: Tổng quan, Camera, Giám sát, Cấu hình, Báo cáo, Nhật ký. Badge đỏ trên Nhật ký = số alarm đang hoạt động | `status_changed` |
| Footer | 30 px | Alarm mới nhất (nền đỏ khi có alarm) / "Không có alarm"; chữ "Giả lập" khi `--sim` | `status_changed` |
| Banner | – | Vàng: "Firmware bản BÀN THỬ" khi `safety_bench_mode` | `status_changed` |

Quy tắc:
- Pill dùng `common.STATE_PILL[state_name] -> (text, kind)`.
- Chip an toàn: `common.safety_problem(st)` trả "" khi ổn, ngược lại thứ tự ưu tiên
  MẤT CÁP LI → E-STOP → CỬA MỞ → THIẾU KHÍ.
- LED Cam1/Cam2: tắt khi chưa có kết quả; xanh khi kết quả gần nhất đúng hạn, vàng khi trễ (`vision_result`).
- Recipe trên header: khi khởi động và mỗi lần `recipe_changed` → job
  `active_recipe_id()` → job `read_recipe(id)` → hiển thị `id · name`.
- `show_page(i)`: đổi trang trong `QStackedWidget`, rồi gọi `page.on_show()` nếu có
  (trang dùng để làm mới dữ liệu khi được mở).
- `closeEvent` → `ctrl.shutdown()` (ghi mẫu sản lượng cuối, dừng thread).

---

## 5. Các trang

### 5.1 Tổng quan – `pages/dashboard.py`

| Khối | Hành vi |
|---|---|
| Điều khiển máy | START / PAUSE / STOP / RESET / HOMING. Bật/tắt theo `common.ENABLED[nút]` ⊇ `state_name`. STOP và HOMING phải `confirm()`. RESET khi `ESTOP` → không gửi lệnh, hiện hướng dẫn thoát E-Stop (nhả nút → đóng cửa → SAFETY_RESET trên tủ). |
| Stepper tốc độ | 60–156 lon/phút, bước 5 → `set_speed` |
| Stepper offset | −18…+18°, bước 0.5 → `set_theta_offset` |
| KPI | Tốc độ thực, Tổng lon, Đạt, Lỗi, Tỉ lệ đạt (+ thanh), Trên băng tải, Mã DM đạt (% + "OK x · NG y"). Số dùng dấu chấm ngăn nghìn. |
| Băng tải | `ConveyorView.set_cans(tracker.snapshot(), belt_mm)` mỗi 100 ms. Màu lon: chờ (xám) / có góc (xanh dương) / đã dán–đạt (xanh lá) / lỗi (đỏ). Mốc: Cam1 50, Chỉnh góc 300, Dán tem 650, S5 950, Cam2 1050, Loại P8 1250, Ra 2000 mm. |
| Camera 1 (mini) | Ảnh cuối của cam1 + overlay + OSD `#id θ` |
| Lon gần nhất | 8 dòng từ `ctrl.recent`: ID, Góc θ, Cam1, Cam2, KQ (NG / Đạt / Chờ) |
| Alarm đang hoạt động | Danh sách `active_alarms(st)`; chỉ dựng lại khi danh sách đổi; rỗng → "Không có alarm"; cảnh báo (ALM 4, 6) vàng, còn lại đỏ |

### 5.2 Camera – `pages/camera.py`

- Chọn camera (`Segmented` "Cam 1 · Góc" / "Cam 2 · DataMatrix") và chế độ ảnh:
  **Trực tiếp** (ảnh mỗi kết quả), **Ảnh cuối** (đóng băng ảnh hiện tại),
  **Ảnh lỗi** (`ctrl.last_ng[cam]`).
- Vẽ lại tối đa 10 Hz: `_on_result` chỉ đặt `_dirty`; timer 100 ms gọi
  `_repaint()` khi `_dirty` và trang đang hiển thị (cờ `_shown` do
  `showEvent/hideEvent`). `on_show()` vẽ ngay.
- Panel phải (QStackedWidget, 1 panel / camera):
  - Cam1: θ lớn (đỏ nếu NG), `QUALITY 0x.. NAME`, thời gian trigger→gửi, trung bình,
    Đạt, Không đạt, Trễ > 80 ms, Lỗi CRC RS485; thông số `cfg.camera1.params`.
  - Cam2: ĐẠT/LỖI, QUALITY, dữ liệu mã đọc được, Grade in (A–F), thời gian,
    Đạt, Không đạt, Trễ > 150 ms, Trùng mã; quy tắc kiểm tra `cfg.camera2.params`.
- Lịch sử: 100 dòng gần nhất của camera đang chọn (`deque(maxlen=100)` mỗi camera).
  Cột QUALITY xanh/đỏ, cột ms vàng khi `late`.

### 5.3 Giám sát – `pages/monitor.py` (4 tab)

| Tab | Nội dung |
|---|---|
| Trạm & trục | 5 thẻ trạm (Cấp lon, Chỉnh góc, Dán tem, Camera 2, Loại P8) suy ra từ `di`; bảng trục M2–M6 (lỗi driver theo bit DI); thẻ an toàn tóm tắt (LedLabel) + biến tần Hz/A |
| Ngõ vào / ra | LED cho từng bit `di` (32) và `do`; bit báo lỗi hiển thị đỏ |
| Chạy tay | Nút Vào/Thoát chế độ chạy tay (`JOG_ENTER/EXIT`). Jog trục: nhấn giữ = `jog(axis, ±1)`, nhả = `jog(axis, 0)`. Xi lanh/van (40012): nút "Đảo" → `set_output(name, !on)`; hạ P5 phải confirm. Chỉ bật khi `state_name == "MANUAL_JOG"`. |
| An toàn | Tile: E-Stop, Cửa, Khí, SAFETY_RESET, Cáp LI, KM1, Phanh M6. Bench mode → "BỎ QUA". Hộp hướng dẫn khi ESTOP. Giá trị thô LI (30024 byte cao) + 8 bit. Bảng kiểm tra đấu dây (giá trị phải đọc ra theo thao tác) với dấu "◀ hiện tại". Trong `--sim` nhấn tile để giả lập. |

Cập nhật ở mọi `status_changed` (4 Hz) → mọi setter phải "chỉ đổi khi khác".

### 5.4 Cấu hình – `pages/config.py` (4 tab)

- **Recipe sản phẩm**: danh sách 16 recipe (đọc khi `on_show`, job "list"),
  form (tên ≤ 15 ký tự, đường kính, chiều cao, góc lệch khoen–tâm tem, M6,
  tốc độ thiết kế, thời gian ép P5, trễ chân không, thổi nhả tem, xung M4;
  hệ số M2 chỉ đọc). Đổi recipe khi đang sửa → confirm "Bỏ thay đổi?".
  Nút: Đọc lại / Lưu / **Lưu & dùng ngay** (confirm; job "saved_active" →
  `recipe_changed.emit()`). Lỗi job → `common.error()`.
- **Xử lý ảnh**: nguồn (`sim`/`external`), thiết bị, ROI, deadline, lưu ảnh NG,
  bảng `params` của từng camera (key/value tự do – thuật toán đọc).
- **Kết nối**: Modbus TCP (IP, port, chu kỳ poll, timeout), RS485 (cổng, baud).
- **Hệ thống**: thư mục dữ liệu, giờ bắt đầu 3 ca, ngôn ngữ, full screen.
- "Lưu cấu hình" → `ctrl.save_settings()`; kết nối/camera áp dụng sau khi khởi động lại.

### 5.5 Báo cáo – `pages/report.py`

- Bộ lọc: Từ ngày, Đến ngày, Ca (Cả ngày / Ca 1..3 theo `cfg.shifts`; ca chỉ
  áp dụng khi chọn 1 ngày, ca cuối kết thúc sang ngày hôm sau).
- KPI: Tổng lon, Đạt, Lỗi, Tỉ lệ đạt, Thời gian chạy (hh:mm), Số lần dừng (tổng alarm).
- Biểu đồ cột theo giờ (đạt/lỗi) – `history.hourly()`.
- Nguyên nhân lỗi (top 6, `history.ng_reasons()`), bảng dừng máy (`history.alarm_counts()`).
- Xuất CSV (UTF-8 BOM, mở được bằng Excel): Giờ, Tổng, Đạt, Lỗi.
- Nguồn số liệu: AppController ghi mẫu `production` mỗi 60 s (delta bộ đếm; bộ
  đếm bị reset → lấy giá trị mới) + mỗi VisionRecord vào bảng `vision`.

### 5.6 Nhật ký – `pages/log.py`

- `LogModel(QAbstractTableModel)` + `LogFilter(QSortFilterProxyModel)`; tối đa 2000 dòng, mới nhất ở trên.
- Lọc nhóm: Tất cả / Alarm / Sự kiện / Vision / Truyền thông; tìm kiếm trong nội dung, mã, nguồn.
- "Tạm dừng cuộn": dòng mới giữ trong `_pending`, thêm khi bỏ tạm dừng.
- Footer: đếm Lỗi / Cảnh báo / Thông tin; nút "Xác nhận / xóa alarm" = `RESET_ALARM`.
- Xuất CSV theo bộ lọc hiện tại.

---

## 6. Quy tắc nghiệp vụ hiển thị

| Quy tắc | Chi tiết |
|---|---|
| Lệnh theo trạng thái | `common.ENABLED` – phải giống `src/app/machine.c` firmware |
| Thoát E-Stop | Chỉ bằng nút SAFETY_RESET vật lý. RESET trên màn hình không thoát E-Stop; sau E-Stop máy về "CẦN HOMING". |
| Lon trên băng tải | `tracker.clear()` khi vào ESTOP hoặc HOMING |
| Lon đạt | Chỉ khi camera 2 OK (camera 1 OK chưa đủ). Kết quả cột "KQ": NG nếu cam1 hoặc cam2 khác OK; "Đạt" nếu cam2 OK; còn lại "Chờ". |
| Kết quả trễ | `elapsed_ms > deadline` (80/150 ms) → `late = True`, log cảnh báo "MCU đã loại lon" |
| Lỗi thuật toán | Ngoại lệ trong `detect()/read()` → NG (cam1 DEFECT, cam2 UNREADABLE), không dừng pipeline |
| Ảnh NG | Lưu `data/ng/YYYYMMDD/cam{n}_{id:05d}_{HHMMSS}.png` nếu `save_ng_images` (không lưu khi `--sim`) |
| Alarm cảnh báo | ALM_04, ALM_06 = vàng (không dừng máy); còn lại đỏ |
| Văn bản nút | Ký tự `&` trong QPushButton/QTabBar phải viết `&&` (Qt hiểu `&` là phím tắt) |

---

## 7. Giao thức RS485_3 (tóm tắt – chi tiết `docs/VISION_PROTOCOL.md`)

| Khung | Hướng | Dài | Nội dung |
|---|---|---|---|
| `55 AA 10 id_lo id_hi crc_lo crc_hi 0A` | MCU→PC | 8 | Trigger camera 1 |
| `55 AA 11 ...` | MCU→PC | 8 | Trigger camera 2 |
| `AA 55 20 id(2) θ×100(int16) Q crc(2) 0D 0A` | PC→MCU | 12 | Kết quả cam1 (`result_cam1`) |
| `AA 55 21 id(2) Q grade len crc(2) 0D 0A` | PC→MCU | 12 | Kết quả cam2 (`result_cam2`) |

CRC16 Modbus, byte thấp trước. `VisionQ`: OK 0, NO_TAB, DEFECT, WRONG_TYPE.
`CodeQ`: OK 0, NO_CODE 0x10, UNREADABLE 0x11, INVALID 0x12, DUPLICATE 0x13, LOW_GRADE 0x14.
Nội dung mã DataMatrix **không** gửi về MCU (chỉ lưu trên PC).

---

## 8. Giao diện & theme

- Bảng màu `theme.C` (nền tối: `bg, panel, panel2, line, text, muted, accent, ok, warn, err, info, off`).
- Tên object/property trong QSS: `card`, `cardTitle`, `kpi`, `stat`, `statePill[kind]`,
  `safetyChip[kind]`, `navBtn`, `seg`, `footer[alarm]`, `benchBanner`, `helpBox`,
  `QPushButton[kind=ok|warn|err|primary][size=sm][big=true]`.
- Đổi màu theo trạng thái: `set_kind(widget, kind)` (property + `repolish`).
- Kích thước chạm tối thiểu: nút 40 px (big 56 px), ô nhập 36 px.
- QSpinBox dùng `NoButtons`: QSS đã đặt `::up-button/::down-button` về 0 px
  (nếu bỏ, ô nhập bị co còn 1 px và mất chữ).
- Widget dùng chung (`ui/widgets.py`): `label(text, objectName)`, `button(text, kind, size, big)`,
  `Card(title, right_widget)` (`.body` là QVBoxLayout), `KpiCard.set(value, sub)`, `Stat.set()`,
  `Led.set(color)`, `LedLabel.set(color, bad)`, `Segmented(items).changed/select()`,
  `ImageView.set_frame(image, overlay, osd_left, osd_right)`, `ConveyorView.set_cans()`, `BarChart.set_data()`.

---

## 9. Điểm merge thuật toán ảnh

Hợp đồng ở `vision/interfaces.py` (không đổi chữ ký):

```python
class FrameSource:      open(); close(); grab(timeout_ms) -> Frame | None   # ảnh của trigger CỨNG gần nhất
class AngleDetector:    detect(frame, params) -> AngleResult(quality: VisionQ, theta_deg, reason, overlay)
class CodeReader:       read(frame, params)   -> CodeResult(quality: CodeQ, data, grade 0..4|None, reason, overlay)
```

- `params` = `settings.cameraN.params` (+ `roi`). Thêm khóa mới vào `settings.py` (mặc định)
  để hiện trên trang Cấu hình → Xử lý ảnh.
- Ngân sách thời gian: grab + thuật toán ≤ 60 ms (cam1) / ≤ 110 ms (cam2).
- Gọi trong thread camera, 1 lon một lần; không chạm Qt; không ném ngoại lệ cho ảnh xấu (trả NG).
- `overlay` dùng `OvCircle / OvLine / OvPolygon / OvText` (tọa độ pixel ảnh) – UI tự vẽ.
- Chọn bản thật: `settings.cameraN.source = "external"`; `--sim` luôn ép `sim`.

---

## 10. Chạy, kiểm thử

```bash
pip install -r requirements.txt
python main.py --sim            # không cần phần cứng
python main.py                  # nối STM32 thật (IP, COM trong config/settings.json)
python -m unittest discover -s tests -t .
```

Kiểm thử không màn hình: `QT_QPA_PLATFORM=offscreen` (test `tests/test_ui_smoke.py`).

## 11. Danh sách kiểm tra khi sửa UI

- [ ] Không gọi I/O (Modbus, serial, file lớn, camera) trong GUI thread.
- [ ] Mọi lệnh máy qua `AppController`, nút bật/tắt theo `ENABLED`.
- [ ] Thao tác nguy hiểm (STOP, HOMING, hạ P5, lưu & dùng recipe) có `confirm()`.
- [ ] Setter chạy theo `status_changed` chỉ cập nhật khi giá trị thay đổi.
- [ ] Bố cục vừa 1024×768, chữ không bị cắt; `&` → `&&` trên nút/tab.
- [ ] Văn bản hiển thị tiếng Việt có dấu; tên biến/hàm tiếng Anh.
- [ ] Chạy `python main.py --sim` và toàn bộ test trước khi commit.
