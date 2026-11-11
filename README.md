# Lab 3 - Giám sát môi trường vườn cây bằng ESP32

## Mô tả
ESP32 đọc nhiệt độ và độ ẩm từ DHT22, gửi dữ liệu qua MQTT,
hiển thị trên giao diện web và điều khiển LED mô phỏng thiết bị.

## Chân kết nối
- DHT22 DATA: GPIO 15
- LED phun sương mô phỏng: GPIO 25
- LED bơm mô phỏng: GPIO 26
- LED quạt mô phỏng: GPIO 27
- DHT22 VCC: 3V3
- DHT22 GND: GND

## Thư viện
- DHT sensor library for ESPx
- PubSubClient

## Chạy mô phỏng
Mở project Wokwi với các file trong thư mục firmware.
Chạy mô phỏng và quan sát dữ liệu tại Serial Monitor.

## Chạy giao diện web
Mở web/index.html bằng trình duyệt.
Firmware và trang web cần dùng cùng broker, DEVICE_ID và topic MQTT.

## Kết quả
Ảnh minh chứng được lưu trong thư mục images.

## Báo cáo
Báo cáo nằm trong thư mục report.