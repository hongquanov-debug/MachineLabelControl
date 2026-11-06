# pmems_config – Cấu hình PMeMS Controller 1 từ PC qua Ethernet

Thư viện + công cụ dòng lệnh Python cho PC Vision. Kết nối tới bộ điều khiển STM32 bằng
**Modbus TCP** (W5500, cổng 502, IP mặc định `192.168.1.200`).

```
PC Vision (Python)
 ├── RS485_3 (UART4, 115200)  → kết quả góc lệch từng lon   (thời gian thực, Mục 2)
 └── Ethernet (Modbus TCP)    → recipe, đường kính lon, trạng thái, sản lượng
```

Hai đường truyền độc lập: thao tác cấu hình không bao giờ chen vào gói kết quả Vision.

## Cài đặt

```bash
pip install -r requirements.txt
```

## Dòng lệnh

```bash
python -m pmems_config --host 192.168.1.200 status
```

```bash
python -m pmems_config --host 192.168.1.200 recipe-list
```

```bash
python -m pmems_config --host 192.168.1.200 recipe-set 5 --name CAN_SLEEK --diameter-mm 58 --height-mm 145 --activate
```

Các lệnh khác: `recipe-get N [--defaults]`, `select N`, `cmd start|stop|pause|reset|home`.

## Dùng trong chương trình (giao diện cảm ứng)

```python
from pmems_config import PmemsClient, PmemsError

with PmemsClient("192.168.1.200") as plc:
    r = plc.read_recipe(1)
    r.diameter_mm = 66.0
    r.tab_offset_deg = 12.5
    try:
        saved = plc.save_recipe(r, activate=True)   # kiểm tra + lưu EEPROM + dùng ngay
        print("He so M2:", saved.m2_scale_x1000)    # bộ điều khiển tự tính D/95
    except PmemsError as e:
        print("Khong luu duoc:", e)                 # vd. máy đang chạy
    st = plc.read_status()
    print(st.state_name, st.ok, st.ng, st.active_alarms)
```

Gợi ý cho GUI: chạy `PmemsClient` trong thread riêng (hoặc QThread), đọc `read_status()`
khoảng 2–5 lần/giây; tách hẳn khỏi thread đọc RS485 của phần xử lý ảnh.

## Quy tắc phía bộ điều khiển

- Recipe được lưu trong **EEPROM của bộ điều khiển**; PC chỉ đọc/ghi.
- Hệ số M2 (`m2_angle_scale_x1000 = D/95×1000`) do bộ điều khiển **tự tính** từ đường kính.
- Không cho sửa/đổi **recipe đang dùng** khi máy ở RUNNING/PAUSED/HOMING (`ERR_STATE`).
  Các recipe khác vẫn sửa được bất cứ lúc nào.
- Khi recipe đang dùng thay đổi ở trạng thái READY, trục M6 tự chạy tới chiều cao mới;
  lệnh START bị giữ lại cho tới khi M6 tới nơi.

## Bản đồ thanh ghi recipe (40021..40041)

Xem chi tiết trong `lib/pmems_core/include/recipe_regs.h`. Cách ghi: một lệnh FC16 ghi
40021..40040 (dữ liệu trước, lệnh ở thanh ghi cuối 40040), sau đó đọc trạng thái ở 40041.

| Thanh ghi | Nội dung |
|---|---|
| 40021 | ID recipe đang sửa (1..16) |
| 40022–40029 | Tên (16 ký tự ASCII) |
| 40030 / 40031 | Đường kính / chiều cao lon (0.1 mm) |
| 40032 | Góc lệch khoen–tem (0.01°, có dấu) |
| 40033 | Vị trí M6 (0.1 mm) |
| 40034 | Hệ số M2 ×1000 (chỉ đọc) |
| 40035–40039 | Tốc độ, overrun M4, thời gian ép, trễ chân không, thời gian thổi |
| 40040 | Lệnh: 1 LOAD, 2 SAVE, 3 SAVE_ACTIVATE, 4 LOAD_DEFAULTS |
| 40041 | Kết quả: 1 OK, 2 ngoài phạm vi, 3 lỗi EEPROM, 4 máy đang chạy, 5 sai ID |

## Kiểm thử

```bash
python -m unittest discover -s tests -t .
```

## An toàn mạng

Modbus TCP không có xác thực: mọi máy trong cùng mạng đều ghi được thanh ghi.
Đặt bộ điều khiển và PC trên **mạng LAN riêng của máy**, không nối thẳng vào mạng văn phòng.
