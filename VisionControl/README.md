# VisionControl – phần mềm PC điều khiển PMeMS Controller 1

Giao diện cảm ứng (PyQt6, 1024×768) cho dây chuyền định hướng lon & dán tem nắp:

- **Ethernet / Modbus TCP** tới STM32F407: lệnh START/STOP/…, recipe, trạng thái, an toàn, sản lượng.
- **RS485_3**: nhận trigger camera (0x10/0x11), gửi kết quả góc (0x20) và kết quả mã DataMatrix (0x21).
- 2 camera: camera 1 định góc khoen, camera 2 kiểm tra mã DataMatrix sau khi dán.
- Lịch sử SQLite, nhật ký, báo cáo theo ca, lưu ảnh NG.

Thuật toán ảnh và driver camera nằm ở project khác và sẽ được merge vào
`visioncontrol/vision/` (xem `CLAUDE.md`). Hiện tại chạy được hoàn toàn bằng giả lập.

## Cài đặt & chạy

```bash
cd VisionControl
pip install -r requirements.txt
```

Chạy giả lập (không cần STM32, camera):

```bash
python main.py --sim
```

Chạy với máy thật (sửa IP/COM ở trang Cấu hình → Kết nối, lưu vào `config/settings.json`):

```bash
python main.py --fullscreen
```

Kiểm thử:

```bash
python -m unittest discover -s tests -t .
```

## Tài liệu

| File | Nội dung |
|---|---|
| `UI_SPEC.md` | Đặc tả xử lý giao diện, luồng dữ liệu, quy tắc (tham chiếu cho AI viết code) |
| `CLAUDE.md` | Hướng dẫn merge thuật toán định góc / đọc mã |
| `pmems_config/README.md` | Thư viện + CLI Modbus TCP, bản đồ thanh ghi |
| `../docs/VISION_PROTOCOL.md` | Khung RS485 giữa MCU và PC |
| `../docs/ui/` | Bản mockup HTML ban đầu |
