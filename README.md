# ESP8266 Monitor

**ESP8266 Monitor** là một dự án mã nguồn mở dựa trên ESP8266, kết hợp chức năng theo dõi thông số môi trường và đóng vai trò như một bảng điều khiển vật lý cho Home Assistant thông qua giao thức MQTT.

Dự án sử dụng màn hình OLED và nút bấm để hiển thị thông số cảm biến tại chỗ, đồng thời cho phép gửi lệnh điều khiển hai chiều tới các thiết bị đã được kết nối trong Home Assistant (ví dụ: quạt thông minh).

## Các tính năng chính

- **Theo dõi môi trường:** Đo nhiệt độ, độ ẩm qua cảm biến AHT10/AHT20 và tính toán điểm sương (dew point). Dữ liệu hỗ trợ chuẩn MQTT Auto-Discovery để Home Assistant tự động nhận diện.
- **Bảng điều khiển từ xa (Remote):** Cung cấp giao diện vật lý điều khiển thiết bị (Bật/Tắt, chỉnh tốc độ, xoay, và hẹn giờ) thông qua hệ thống menu hiển thị trên màn hình OLED (SSD1306) với 3 nút bấm (UP, DOWN, OK).
- **Đồng bộ trạng thái hai chiều:** Trạng thái thiết bị hiển thị trên màn hình được đồng bộ theo thời gian thực với Home Assistant. Mạch sẽ tự động gửi yêu cầu đồng bộ (Sync Request) mỗi khi khởi động lại hoặc sau khi kết nối lại MQTT.
- **Web Portal cấu hình:** Hỗ trợ chế độ Access Point ở lần khởi động đầu tiên để thiết lập kết nối WiFi và thông số MQTT Broker. Dữ liệu cấu hình được lưu cục bộ trên LittleFS.
- **Đồng bộ thời gian:** Tự động lấy và cập nhật thời gian từ máy chủ NTP để hiển thị trên giao diện.

## Yêu cầu phần cứng

- Mạch Wemos D1 Mini (ESP8266) hoặc các board tương đương
- Màn hình OLED I2C SSD1306 128x64
- Cảm biến nhiệt độ, độ ẩm AHT10 hoặc AHT20
- 3x Nút bấm nhấn nhả (Push buttons)

## Sơ đồ chân cắm

| Linh kiện                | Chân (Wemos D1 Mini) | GPIO   | Ghi chú                            |
| :----------------------- | :------------------- | :----- | :--------------------------------- |
| **I2C SDA (OLED & AHT)** | D2                   | GPIO4  | I2C mặc định                       |
| **I2C SCL (OLED & AHT)** | D1                   | GPIO5  | I2C mặc định                       |
| **Nút UP**               | D3                   | GPIO0  | Kích hoạt mức thấp (Kéo xuống GND) |
| **Nút DOWN**             | D7                   | GPIO13 | Kích hoạt mức thấp (Kéo xuống GND) |
| **Nút OK**               | D4                   | GPIO2  | Kích hoạt mức thấp (Kéo xuống GND) |

_(Xem file `src/Globals.h` để biết chi tiết khai báo và thay đổi nếu cần)_

## Hướng dẫn cài đặt

1. Mở dự án bằng Visual Studio Code có tiện ích [PlatformIO](https://platformio.org/).
2. Cắm mạch ESP8266 vào, chọn **PlatformIO: Build** rồi **Upload**.
3. **Cấu hình WiFi/MQTT:** Ở lần khởi động đầu tiên, dùng điện thoại kết nối vào WiFi do ESP phát ra. Truy cập `http://192.168.4.1` để điền thông tin mạng và MQTT Broker của bạn.

---

## Tích hợp Bảng điều khiển Quạt vào Home Assistant

Trong dự án này, ESP **không gắn trực tiếp với quạt vật lý** mà chỉ là một bảng điều khiển từ xa. Để các nút bấm trên ESP có thể điều khiển được chiếc quạt thực tế của bạn (ví dụ thực thể `fan.living_room` trong HA) và ngược lại, màn hình ESP có thể cập nhật trạng thái nếu quạt được điều khiển từ nơi khác, bạn cần tạo các Automation (Tự động hóa) trong Home Assistant.

**Lưu ý:** Thay `esp_monitor_XXXXXX` bằng Client ID thực tế của bạn (xem qua Serial log hoặc MQTT Explorer, thường là `esp_monitor_` + mã chip ESP).

### 1. Automation: ESP gửi lệnh điều khiển Quạt trên HA (CẤU HÌNH MẪU)

Khi bạn thao tác trên màn hình ESP, thiết bị sẽ gửi toàn bộ trạng thái (dưới dạng JSON) lên topic `.../state`. Automation này nhận JSON đó, phân tích và điều khiển các thực thể tương ứng (quạt, hẹn giờ) trên HA.

> **ĐÂY LÀ CẤU HÌNH MẪU**: Bạn cần thay thế `esp_monitor_XXXXXX` bằng Client ID của bạn, và đổi `fan.your_fan_entity`, `timer.your_fan_timer` cho khớp với thiết bị thực tế.

```yaml
alias: "ESP Monitor: Nhận lệnh điều khiển từ ESP (Cấu hình mẫu)"
description: Đồng bộ lệnh từ ESP về Home Assistant
triggers:
  - trigger: mqtt
    options:
      topic: esp_monitor/esp_monitor_XXXXXX/state
conditions: []
actions:
  - choose:
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload_json.POWER == 'ON' }}"
        sequence:
          - action: fan.turn_on
            target:
              entity_id: fan.your_fan_entity
            data:
              percentage: >
                {% set speed = trigger.payload_json.FanSpeed | int(1) %} {{ 33
                if speed == 1 else (66 if speed == 2 else 100) }}
          - action: fan.oscillate
            target:
              entity_id: fan.your_fan_entity
            data:
              oscillating: "{{ trigger.payload_json.Oscillate == 'ON' }}"
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload_json.POWER == 'OFF' }}"
        sequence:
          - action: fan.turn_off
            target:
              entity_id: fan.your_fan_entity
            data: {}
  - choose:
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload_json.Timer | int(-1) > as_timestamp(now()) }}"
        sequence:
          - action: timer.start
            target:
              entity_id: timer.your_fan_timer
            data:
              duration: >-
                {{ (trigger.payload_json.Timer | int - as_timestamp(now()) |
                int) }}
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload_json.Timer | int(-1) == -1 }}"
        sequence:
          - action: timer.cancel
            target:
              entity_id: timer.your_fan_timer
            data: {}
mode: queued
```

### 2. Automation: Đồng bộ trạng thái từ HA về ESP (CẤU HÌNH MẪU)

Automation này sẽ bao gồm cả hai nhiệm vụ:

- Phản hồi khi ESP yêu cầu đồng bộ (Sync Request lúc mới khởi động).
- Chủ động cập nhật ESP khi Quạt hoặc Hẹn giờ bị đổi trạng thái từ nơi khác (App, Remote).

> **ĐÂY LÀ CẤU HÌNH MẪU**: Bạn cần thay thế `esp_monitor_XXXXXX` bằng Client ID của bạn, và đổi các thực thể `fan.your_fan_entity`, `timer.your_fan_timer` cho khớp với thiết bị thực tế trên Home Assistant của bạn.

```yaml
alias: "ESP Monitor: Đồng bộ trạng thái Quạt (Cấu hình mẫu)"
description: "Gửi trạng thái (kèm retain) cho ESP khi có yêu cầu Sync hoặc khi quạt/timer thay đổi"
mode: single
triggers:
  - trigger: mqtt
    options:
      topic: esp_monitor/esp_monitor_XXXXXX/sync
      payload: State
      value_template: "{{ value_json.Request }}"
  - trigger: state
    entity_id:
      - fan.your_fan_entity
  - trigger: state
    entity_id:
      - timer.your_fan_timer
conditions: []
actions:
  - action: mqtt.publish
    metadata: {}
    data:
      qos: "0"
      topic: esp_monitor/esp_monitor_XXXXXX/command/state
      retain: true
      payload: |-
        {% set pct = state_attr('fan.your_fan_entity', 'percentage') | int(0) %}
        {% set speed = 1 if pct <= 34 else (2 if pct <= 67 else 3) %}

        {% set timer_epoch = -1 %}
        {% if is_state('timer.your_fan_timer', 'active') %}
          {% set end_time = state_attr('timer.your_fan_timer', 'finishes_at') %}
          {% if end_time %}
            {% set timer_epoch = as_timestamp(end_time) | int(-1) %}
          {% endif %}
        {% endif %}

        {{
          {
            "POWER": "ON" if is_state('fan.your_fan_entity', 'on') else "OFF",
            "FanSpeed": speed,
            "Oscillate": "ON" if state_attr('fan.your_fan_entity', 'oscillating') else "OFF",
            "Timer": timer_epoch
          } | tojson
        }}
```
