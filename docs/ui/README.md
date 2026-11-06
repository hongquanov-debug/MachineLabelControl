# Giao diện PC – bản mẫu HTML (1024 × 768)

Bản mẫu (mockup) giao diện phần mềm PC điều khiển dây chuyền, dùng làm **bản vẽ tham chiếu**
để viết lại bằng **Python + PyQt6**. Dữ liệu trong bản mẫu là giả lập (lon chạy trên băng
tải, ảnh nắp lon, alarm ngẫu nhiên), giúp thấy giao diện khi máy đang chạy.

| File | Nội dung |
|---|---|
| `index.html` | Cấu trúc 6 trang (mỗi `<section class="page">` = một trang của `QStackedWidget`) |
| `styles.css` | Màu, kích thước, bố cục. Biến ở `:root` dùng lại được trong file QSS |
| `app.js` | Chuyển trang, giả lập dữ liệu, xử lý form. Mỗi khối có ghi chú widget PyQt6 tương ứng |

**Mở xem:** nhấp đúp `index.html` (Chrome/Edge). Không cần Internet, không cần server.
Khung ứng dụng cố định **1024 × 768**, nút bấm cao ≥ 44 px để dùng tay trên màn hình cảm ứng.

---

## 1. Bố cục chung

```
┌───────────────────────────── 1024 ─────────────────────────────┐
│ HEADER 52px: logo │ TRẠNG THÁI │ recipe │ tốc độ │ kết nối │ giờ │
├──────┬─────────────────────────────────────────────────────────┤
│ NAV  │                                                         │
│ 92px │              NỘI DUNG  932 × 686  (QStackedWidget)      │
│ 6    │                                                         │
│ nút  │                                                         │
├──────┴─────────────────────────────────────────────────────────┤
│ FOOTER 30px: alarm đang hoạt động (nền đỏ khi có alarm)        │
└─────────────────────────────────────────────────────────────────┘
```

| Vùng | Widget PyQt6 |
|---|---|
| Toàn cửa sổ | `QMainWindow`, `setFixedSize(1024, 768)` hoặc `showFullScreen()` trên PC công nghiệp |
| Header | `QWidget` + `QHBoxLayout`; ô trạng thái là `QLabel` đổi `objectName`/property để đổi màu bằng QSS |
| Menu trái | 6 `QToolButton` (icon trên, chữ dưới: `Qt.ToolButtonTextUnderIcon`) trong `QButtonGroup(exclusive=True)` |
| Nội dung | `QStackedWidget`, mỗi trang là một class `QWidget` riêng |
| Footer | `QStatusBar` hoặc `QFrame` cao 30 px |
| Hộp xác nhận | `QMessageBox.question()` (STOP, HOMING, đổi recipe…) |

---

## 2. Các trang và nguồn dữ liệu

Nguồn dữ liệu là thư viện `tools/pc_config` (`PmemsClient`, Modbus TCP qua Ethernet)
và module xử lý ảnh của bạn.

### 2.1. Tổng quan

| Thành phần | Widget | Dữ liệu |
|---|---|---|
| Nút START / PAUSE / STOP / RESET / HOMING | `QPushButton` (cao 56 px) | `plc.send_command(MachineCmd.START)`… → 40001 |
| Tốc độ đặt ±5 | 2 nút + `QLabel` | `plc.set_speed()` → 40003 |
| Offset góc ± | 2 nút + `QLabel` | `plc.set_theta_offset()` → 40004 |
| 7 ô KPI | `QFrame` + 2 `QLabel` | `read_status()`: `speed_cans_min`, `fed`, `ok`, `ng`, `cans_on_belt` |
| Sơ đồ băng tải | `QGraphicsView` / widget tự vẽ bằng `QPainter` | Vị trí từng lon: tính ở PC từ thời điểm Vision + tốc độ (firmware chưa xuất từng lon) |
| Camera thu nhỏ | `QLabel.setPixmap()` | Khung hình cuối cùng của module Vision |
| Lon gần nhất | `QTableView` + model | Kết quả Cam1 (góc) và Cam2 (mã) theo CAN_ID, lưu ở PC |
| Alarm | `QListWidget` | `status.active_alarms` (30019–30020) |

### 2.2. Camera (2 camera)

Nút chọn trên khung ảnh: **Camera 1 · Góc lệch** / **Camera 2 · DataMatrix** (`QButtonGroup`
đổi trang của một `QStackedWidget` nhỏ ở cột phải và bảng lịch sử). Giao thức của cả hai camera:
[`docs/VISION_PROTOCOL.md`](../VISION_PROTOCOL.md). Ảnh và kết quả lấy từ project xử lý ảnh của bạn.

**Camera 1** (gói `0x10` / `0x20`):

| Thành phần | Widget | Dữ liệu |
|---|---|---|
| Ảnh trực tiếp + lớp phủ (vòng nắp, tia góc, chữ) | `QLabel` + `QPainter` vẽ lên `QImage` (hoặc `pyqtgraph.ImageView`) | Ảnh từ SDK Hikrobot (MVS) → numpy → `QImage` |
| Trực tiếp / Ảnh cuối / Ảnh lỗi | 3 `QPushButton` checkable trong `QButtonGroup` | — |
| Kết quả θ, thời gian xử lý, thống kê | `QLabel` | Module xử lý ảnh; số lỗi CRC: 30021 |
| Phơi sáng / Gain / Ngưỡng | `QSlider` + `QLabel` | Cấu hình camera / thuật toán |
| Lịch sử nhận diện | `QTableView` | Mỗi gói 0x20 gửi về MCU qua RS485_3 |

**Camera 2 – DataMatrix** (gói `0x11` / `0x21`):

| Thành phần | Widget | Dữ liệu |
|---|---|---|
| Ảnh tem + khung mã (xanh OK / cam đọc được nhưng bị từ chối / đỏ đứt nét không thấy mã) + chuỗi giải mã ở đáy ảnh | `QLabel` + `QPainter` | Ảnh camera 2, vị trí mã từ bộ giải mã |
| ĐẠT / LỖI + QUALITY_FLAG | `QLabel` | Kết quả gửi trong gói 0x21 (`CodeQ`) |
| Chuỗi dữ liệu | `QLabel` font mono, `setWordWrap(True)` | Chuỗi giải mã (chỉ có ở PC, MCU không nhận) |
| Grade, thời gian, Đạt / Không đạt / Timeout / Trùng mã | `QLabel` | Thống kê ở PC; tổng từ MCU: 30025–30029 |
| Quy tắc kiểm tra | `QFormLayout` chỉ đọc + nút "Sửa quy tắc" mở trang Cấu hình | GTIN theo recipe, độ dài, grade tối thiểu, phạm vi kiểm tra trùng |
| Lịch sử đọc mã | `QTableView` + model | **Lưu SQLite**: thời gian, CAN_ID, chuỗi, grade, QUALITY, recipe → truy xuất nguồn gốc |

Hạn trả lời camera 2 là **150 ms**; mục "Thời gian tối đa" ở Cấu hình nên để ~110 ms.


### 2.3. Giám sát (3 tab: `QTabWidget`)

| Tab | Thành phần | Dữ liệu |
|---|---|---|
| Trạm & trục | 5 thẻ trạm, bảng 5 trục, an toàn, biến tần | `read_status()`: DI/DO, `m6_height_mm`, `vfd_freq_hz`, `vfd_current_a`, `air_ok` |
| Ngõ vào / ra | 32 + 24 "đèn LED" (`QLabel` tròn đổi màu) | `status.di` (30012–13), `status.do` (30014–15); tên bit lấy từ `src/app/io_map.h` |
| Chạy tay | Chọn trục, nút giữ ◀ / ▶, xi lanh | 40001 = 6/7 (vào/thoát JOG), 40010 = trục, 40011 = −1/0/+1. Nút giữ: dùng tín hiệu `pressed` / `released` của `QPushButton` |

Xi lanh / van bằng tay: thanh ghi **40012** (bit n = DO n, 0..14), chỉ nhận ở MANUAL_JOG.
Python: `plc.set_output("P4", True)`, `plc.read_manual_outputs()`. Bị từ chối (ngoại lệ 04) khi
không ở chạy tay hoặc khi hạ P5 lúc mâm M3 đang quay → hiện `QMessageBox.warning`.
Cờ `status.safety_bench_mode` = True → hiện băng cảnh báo đỏ "CHẾ ĐỘ BÀN THỬ – KHÔNG CÓ AN TOÀN".

### 2.4. Cấu hình (4 tab)

| Tab | Nội dung | Dữ liệu |
|---|---|---|
| Recipe | Danh sách 16 recipe + form | `plc.read_recipe(id)`, `plc.save_recipe(r, activate)`; kiểm tra phía PC bằng `Recipe.validate()` |
| Xử lý ảnh | Camera, thuật toán | File cấu hình riêng của PC (JSON/INI) |
| Kết nối | IP/port Modbus TCP, cổng COM RS485 | File cấu hình của PC |
| Hệ thống | Người dùng, ca, thư mục dữ liệu | File cấu hình của PC |

Form recipe:
- Mỗi ô là `QDoubleSpinBox` / `QSpinBox` với `setRange()` theo đúng giới hạn firmware
  (đường kính 30–120, M6 0–98…), nên người dùng không nhập được giá trị sai.
- Ô "Hệ số M2" chỉ đọc, tự tính `round(D × 1000 / 95)` khi đổi đường kính (bộ điều khiển cũng tự tính lại).
- Có thay đổi chưa lưu → hiện "● Có thay đổi chưa lưu", hỏi lại khi chuyển recipe khác.
- Lỗi `ERR_STATE` (máy đang chạy) → `QMessageBox.warning`.

### 2.5. Báo cáo

| Thành phần | Widget | Dữ liệu |
|---|---|---|
| Lọc ngày / ca / recipe | `QDateEdit`, `QComboBox` | Truy vấn cơ sở dữ liệu của PC |
| 6 ô KPI | `QLabel` | Tổng hợp từ CSDL |
| Sản lượng theo giờ | `pyqtgraph.BarGraphItem` hoặc `QtCharts.QStackedBarSeries` | PC ghi mẫu `fed/ok/ng` mỗi phút vào SQLite |
| Nguyên nhân lỗi | `QProgressBar` hoặc biểu đồ ngang | Đếm theo loại lỗi Vision / alarm |
| Xuất Excel / PDF | `openpyxl`, `QPrinter` / `QTextDocument.print_()` | — |

> Firmware chỉ có bộ đếm tổng (30004–30009). Báo cáo theo giờ/ca do **PC tự lưu**:
> mỗi phút đọc bộ đếm, ghi hiệu số vào SQLite.

### 2.6. Nhật ký

| Thành phần | Widget |
|---|---|
| Lọc Tất cả / Alarm / Sự kiện / Vision / Truyền thông | `QButtonGroup` |
| Tìm kiếm | `QLineEdit` → `QSortFilterProxyModel.setFilterFixedString()` |
| Bảng | `QTableView` + `QAbstractTableModel` (đừng dùng `QTableWidget` khi có hàng nghìn dòng) |
| Tạm dừng cuộn, xuất CSV, xác nhận alarm | `QPushButton`; RESET alarm = 40001 = 4 |

Sự kiện được ghi khi PC phát hiện thay đổi: trạng thái FSM (30001), bit alarm (30019–20),
kết quả Vision NG/timeout, mất kết nối.

### 2.7. An toàn (ô trên thanh tiêu đề + tab Giám sát → An toàn)

Dữ liệu: `read_status()` → `estop_ok`, `door_closed`, `air_ok_sw`, `reset_pressed`, `io_bus_ok`,
`safety_ext_raw` (thanh ghi **30024**), `safety_bench_mode`, `safety_via_li_ext` (30023 bit3, bit4).

| Thành phần | Widget | Hiển thị |
|---|---|---|
| Ô an toàn trên thanh tiêu đề | `QLabel` đổi property `state` = ok / bad / bench | "AN TOÀN OK" (xanh), "E-STOP" / "CỬA MỞ" / "THIẾU KHÍ" / "MẤT CÁP LI" (đỏ, nhấp nháy), "BÀN THỬ" (cam) |
| Băng cảnh báo bàn thử | `QLabel` nền sọc cam phía trên vùng nội dung, `setVisible(status.safety_bench_mode)` | Luôn hiện khi firmware là bản `controller1_bench` |
| 5 ô tín hiệu | `QFrame` + 2 `QLabel` (tên, trạng thái kèm chữ) | Đèn + chữ: "Đã nhả / ĐANG NHẤN", "Đóng / ĐANG MỞ"… và nguồn (LI mở rộng bit n / GPIO) |
| KM1, phanh M6 | `QFrame` | Suy ra từ trạng thái máy: KM1 cắt ở E-STOP; "KHÓA TẮT" ở bản bàn thử |
| Hướng dẫn thoát E-Stop | `QLabel` nền đỏ, chỉ hiện ở E-STOP | Nhả E-Stop → đóng cửa → nhấn **SAFETY_RESET trên tủ** |
| Byte thô LI mở rộng | `QLabel` hex + 8 ô bit | `safety_ext_raw` (byte cao 30024), dùng khi nghiệm thu đấu dây |
| Bảng kiểm tra đấu dây | `QTableWidget` 6 dòng | Đánh dấu "◀ hiện tại" ở dòng khớp với tín hiệu đang đọc |
| Thẻ tóm tắt (tab Trạm & trục) | 6 đèn LED nhỏ | E-Stop, cửa, khí, reset, cáp LI, KM1 |

Quy tắc giao diện (giống firmware, `src/app/machine.c`):
- Nút RESET trên màn hình (40001 = 4) **không** đưa máy ra khỏi E-STOP; chỉ nút SAFETY_RESET vật lý
  làm được. Khi người vận hành bấm RESET lúc E-STOP, hiện hướng dẫn thay vì gửi lệnh vô ích.
- Bật/tắt nút điều khiển theo trạng thái: START chỉ ở READY/PAUSE, HOMING chỉ ở
  HOMING_REQUIRED/READY/ALARM… (`updateButtons()` trong `app.js`).
- Giao diện chỉ **hiển thị** an toàn, không bao giờ ghi gì vào chuỗi an toàn. Trong bản mẫu, nhấn vào
  ô tín hiệu để giả lập; bản PyQt6 không có thao tác này.

Xem thử các chế độ trong bản mẫu: mở `index.html?bench=1` (bản bàn thử) hoặc
`index.html?src=gpio` (PCB v2, an toàn qua GPIO).

---

## 3. Kiến trúc chương trình PyQt6 đề xuất

**Nguyên tắc vàng: không bao giờ chờ đợi trong luồng giao diện.** Đọc Modbus, chụp ảnh,
gửi RS485 đều chạy ở luồng phụ, gửi kết quả về giao diện bằng signal.

```
                     ┌──────────────── Luồng giao diện (GUI) ────────────────┐
                     │  MainWindow                                             │
 ModbusWorker ──────►│   status_updated(MachineStatus) → cập nhật header, KPI, │
 (QThread, 250 ms)   │                                    I/O, alarm, log      │
   PmemsClient       │   lệnh nút bấm → signal → ModbusWorker.send_command()   │
                     │                                                         │
 VisionWorker ──────►│   frame_ready(QImage, Result) → trang Camera, Tổng quan │
 (QThread)           │                                                         │
   camera SDK        └─────────────────────────────────────────────────────────┘
   + thuật toán
   + RS485_3 (pyserial): đọc gói 0x10 từ MCU, gửi gói 0x20 kết quả
```

Gợi ý cấu trúc thư mục Python:

```
pc_app/
├── main.py                 # QApplication, nạp QSS, MainWindow.show()
├── ui/
│   ├── main_window.py      # header, nav, QStackedWidget, footer
│   ├── page_dashboard.py
│   ├── page_camera.py
│   ├── page_monitor.py
│   ├── page_config.py
│   ├── page_report.py
│   ├── page_log.py
│   ├── widgets/            # ConveyorView, LedLabel, KpiCard...
│   └── style.qss
├── workers/
│   ├── modbus_worker.py    # dùng tools/pc_config/pmems_config
│   └── vision_worker.py
├── data/
│   ├── history_db.py       # SQLite: sản lượng theo phút, nhật ký
│   └── settings.py         # đọc/ghi cấu hình PC
└── requirements.txt        # PyQt6, pyqtgraph, pymodbus, pyserial, numpy, opencv-python
```

---

## 4. Chuyển CSS sang QSS

QSS (Qt Style Sheets) gần giống CSS. Ví dụ tương ứng với `styles.css`:

```css
QMainWindow, QWidget { background: #0e1318; color: #e6edf3;
                       font-family: "Segoe UI"; font-size: 13px; }
QFrame#card { background: #171e26; border: 1px solid #2a3542; border-radius: 8px; }

QPushButton { min-height: 44px; padding: 0 14px; border-radius: 6px;
              background: #1e2731; border: 1px solid #2a3542; font-weight: 600; }
QPushButton#btnStart { background: #15803d; border-color: #16a34a; }
QPushButton#btnStop  { background: #b91c1c; border-color: #dc2626; }
QPushButton#btnPause { background: #b7791f; border-color: #d69e2e; }
QPushButton:disabled { color: #5b6673; }

QLabel#statePill[state="run"]   { color: #22c55e; border: 1px solid #22c55e; border-radius: 17px; }
QLabel#statePill[state="alarm"] { color: #ef4444; border: 1px solid #ef4444; border-radius: 17px; }

QTableView { background: #171e26; gridline-color: #2a3542; alternate-background-color: #1a222b; }
QHeaderView::section { background: #212b36; color: #8b98a6; border: 0; padding: 6px; }
```

Đổi màu theo trạng thái: `label.setProperty("state", "run")`, rồi gọi
`label.style().unpolish(label); label.style().polish(label)` để QSS áp dụng lại.

| Biến CSS | Giá trị | Dùng cho |
|---|---|---|
| `--bg` | `#0e1318` | Nền ứng dụng |
| `--panel` | `#171e26` | Nền khung |
| `--panel-2` | `#1e2731` | Ô nhập, nút thường |
| `--line` | `#2a3542` | Viền |
| `--accent` | `#3b8bf6` | Nút chính, mục đang chọn |
| `--ok` / `--warn` / `--err` | `#22c55e` / `#f5a524` / `#ef4444` | Chạy-đạt / cảnh báo / lỗi |

---

## 5. Lưu ý cho màn hình cảm ứng 1024 × 768

- Nút ≥ 44 px, khoảng cách ≥ 8 px. Nút nguy hiểm (STOP, HOMING, đổi recipe) luôn có hộp xác nhận.
- Bàn phím ảo: dùng `QT_IM_MODULE=qtvirtualkeyboard` (cần gói Qt Virtual Keyboard),
  hoặc bàn phím ảo của Windows (`osk.exe`) khi ô nhập được chọn.
- Windows có thể phóng to giao diện (DPI scaling 125 %…). Để khung đúng 1024 × 768, đặt
  `QApplication.setHighDpiScaleFactorRoundingPolicy(...)` hoặc tắt scaling cho ứng dụng.
- Font hệ thống `Segoe UI` (Windows) hiển thị tiếng Việt đầy đủ; số liệu dùng `Consolas`.
- Màu không phải là cách DUY NHẤT để báo trạng thái: luôn kèm chữ (RUNNING, ALARM…), vì
  ngoài xưởng có thể chói sáng hoặc người vận hành nhìn kém màu.
