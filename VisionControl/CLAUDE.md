# CLAUDE.md – hướng dẫn cho AI làm việc trong VisionControl

Phần mềm PC (Python 3.10+, PyQt6) cho dây chuyền PMeMS Controller 1.
**Đọc `UI_SPEC.md` trước khi sửa code** – đó là đặc tả hành vi UI, luồng dữ liệu,
quy tắc thread và quy tắc nghiệp vụ.

## Lệnh

```bash
pip install -r requirements.txt
python main.py --sim                       # chạy với MCU + camera giả lập
python -m unittest discover -s tests -t .  # chạy trong thư mục VisionControl
```

Không có màn hình (CI/container): `export QT_QPA_PLATFORM=offscreen`.

## Quy tắc bất biến

1. Trang UI chỉ nói chuyện với `AppController` (method + signal). Không gọi Modbus/serial/camera trực tiếp.
2. Không làm I/O trong GUI thread; thread khác → GUI chỉ qua `pyqtSignal`.
3. Kết quả vision gửi về MCU (`link.write`) **trước** khi cập nhật UI; đừng đưa đường
   trigger→kết quả qua GUI thread.
4. Không đổi định dạng khung RS485 (`core/vision_protocol.py`) hay địa chỉ thanh ghi
   (`pmems_config/registers.py`) nếu firmware (`../src`) chưa đổi tương ứng; test
   `tests/test_vision_protocol.py` chứa vector lấy từ firmware.
5. `common.ENABLED` phải giống luật trạng thái của firmware (`../src/app/machine.c`).
6. Chữ hiển thị tiếng Việt; `&` trong nút/tab viết `&&`; giao diện phải vừa 1024×768.

## Merge thuật toán ảnh (việc sẽ làm sau)

Người dùng sẽ gửi code định góc (camera 1), đọc DataMatrix (camera 2) và có thể cả
driver camera. Chỉ sửa 3 file, giữ nguyên hợp đồng ở `visioncontrol/vision/interfaces.py`:

| File | Lớp | Việc |
|---|---|---|
| `vision/camera_source.py` | `ExternalCameraSource(FrameSource)` | `open/close`, `grab(timeout_ms)` trả ảnh của trigger cứng gần nhất (`Frame`) hoặc `None` |
| `vision/angle_detector.py` | `TabAngleDetector(AngleDetector)` | `detect(frame, params) -> AngleResult(VisionQ, theta_deg, reason, overlay)` |
| `vision/code_reader.py` | `DataMatrixReader(CodeReader)` | `read(frame, params) -> CodeResult(CodeQ, data, grade, reason, overlay)` |

Các bước:
1. Đặt code của người dùng vào package con (ví dụ `visioncontrol/vision/algo/`), viết lớp
   adapter trong 3 file trên chuyển đổi input/output sang `Frame` / `AngleResult` / `CodeResult`.
2. Tham số mới → thêm mặc định vào `settings.py` (`camera1.params` / `camera2.params`);
   trang Cấu hình → Xử lý ảnh tự hiển thị.
3. Thư viện mới → `requirements.txt`.
4. Giữ ngân sách thời gian ≤ 60 ms (cam1) / ≤ 110 ms (cam2); không ném ngoại lệ với ảnh xấu
   (pipeline vẫn bắt và trả NG nhưng sẽ log "lỗi thuật toán").
5. Thêm test cho adapter với vài ảnh mẫu (`tests/test_vision_*.py`), giữ test sim chạy được.
6. Chạy thử: đặt `"source": "external"` trong `config/settings.json`; `--sim` luôn dùng bản giả lập.

## Bản đồ nhanh

- `controller.py` – AppController (signals, lệnh, thống kê, lịch sử, lưu ảnh NG)
- `workers/` – PlcWorker (QThread Modbus), SerialLink/SimLink (RS485), CameraPipeline
- `sim/sim_mcu.py` – STM32 giả lập, cùng API `pmems_config.PmemsClient`
- `ui/pages/*.py` – 6 trang; `ui/widgets.py`, `ui/theme.py`, `ui/common.py`
