#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <Adafruit_AHTX0.h>

#if defined(ESP8266)
#define BTN_DOWN 13
#define BTN_UP 0
#define BTN_OK 2

#elif defined(ESP32)
#define BTN_DOWN 13
#define BTN_UP 12
#define BTN_OK 14
#endif

Adafruit_AHTX0 aht;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
unsigned long lastReadTime = 0;

struct Button
{
  const uint8_t pin;
  const char *name;
  bool state;
  unsigned long lastPressTime;
};
const unsigned long DEBOUNCE_DELAY = 100;

Button btns[] = {
    {BTN_DOWN, "DOWN", HIGH, 0},
    {BTN_UP, "UP", HIGH, 0},
    {BTN_OK, "OK", HIGH, 0}};

float calculateDewPoint(float t, float h)
{
  float temp = (17.271 * t) / (237.7 + t) + log(h / 100.0);
  return (237.7 * temp) / (17.271 - temp);
}

void setup()
{
  Serial.begin(115200);

  for (int i = 0; i < 3; i++)
  {
    pinMode(btns[i].pin, INPUT_PULLUP);
  }

  if (!aht.begin())
  {
    Serial.println("Could not find AHT? Check wiring");
  }

  u8g2.begin();
  u8g2.enableUTF8Print();
}

void drawDegreeCValue(float value, int rightX, int y)
{
  String valStr = String(value, 1);
  const char *cStr = "C";
  int circleSpace = 6;
  int totalWidth = u8g2.getUTF8Width(valStr.c_str()) + circleSpace + u8g2.getUTF8Width(cStr);
  int startX = rightX - totalWidth;

  u8g2.setCursor(startX, y);
  u8g2.print(valStr);
  // Vẽ vòng tròn nhỏ (bán kính 2) làm ký hiệu độ
  int circleX = startX + u8g2.getUTF8Width(valStr.c_str()) + 2;
  int circleY = y - 10; // đặt ở phía trên, gần đỉnh chữ số
  u8g2.drawCircle(circleX, circleY, 2);
  // In "C" sau vòng tròn
  u8g2.setCursor(startX + u8g2.getUTF8Width(valStr.c_str()) + circleSpace, y);
  u8g2.print(cStr);
}

// Mock trạng thái (sẽ thay bằng dữ liệu thật sau)
bool mockWifiConnected = true;
int mockWifiStrength = 3; // 0-3: số vạch sóng
bool mockMqttConnected = true;

void drawStatusBar()
{
  // --- WiFi icon (góc trên trái) ---
  // Vẽ các vạch sóng từ thấp đến cao
  int barX = 2;
  int barBottomY = 10;
  for (int i = 0; i < 4; i++)
  {
    int barHeight = 3 + i * 2; // cao dần: 3, 5, 7, 9
    int barY = barBottomY - barHeight;
    if (mockWifiConnected && i < mockWifiStrength)
    {
      u8g2.drawBox(barX + i * 4, barY, 3, barHeight); // vạch đặc
    }
    else
    {
      u8g2.drawFrame(barX + i * 4, barY, 3, barHeight); // vạch rỗng
    }
  }

  // --- MQTT icon (cạnh WiFi) ---
  int mqttX = barX + 4 * 4 + 4; // sau icon WiFi
  if (mockMqttConnected)
  {
    u8g2.drawDisc(mqttX + 3, 6, 3); // chấm tròn đặc = connected
  }
  else
  {
    u8g2.drawCircle(mqttX + 3, 6, 3); // chấm tròn rỗng = disconnected
  }

  // --- Time (góc trên phải) ---
  u8g2.setFont(u8g2_font_6x12_tr); // font nhỏ cho status bar
  const char *timeStr = "12:34";
  int timeWidth = u8g2.getStrWidth(timeStr);
  u8g2.setCursor(126 - timeWidth, 10);
  u8g2.print(timeStr);

  // --- Đường kẻ phân tách ---
  u8g2.drawHLine(0, 13, 128);

  // Khôi phục font cho nội dung chính
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}

void loop()
{
  unsigned long now = millis();
  if (now - lastReadTime >= 1000)
  {
    sensors_event_t humidity, temp_event;
    aht.getEvent(&humidity, &temp_event);

    float temp = temp_event.temperature;
    float hum = humidity.relative_humidity;
    float dewPoint = calculateDewPoint(temp, hum);

    u8g2.clearBuffer();

    // --- Thanh trạng thái ---
    drawStatusBar();

    // --- Dòng 1: Nhiệt độ ---
    u8g2.setCursor(4, 28);
    u8g2.print("Nhiệt độ");
    drawDegreeCValue(temp, 124, 28);
    u8g2.drawHLine(4, 32, 120);

    // --- Dòng 2: Độ ẩm ---
    u8g2.setCursor(4, 46);
    u8g2.print("Độ ẩm");
    String humStr = String(hum, 1) + "%";
    u8g2.setCursor(124 - u8g2.getUTF8Width(humStr.c_str()), 46);
    u8g2.print(humStr);
    u8g2.drawHLine(4, 50, 120);

    // --- Dòng 3: Điểm sương ---
    u8g2.setCursor(4, 63);
    u8g2.print("Đ.sương");
    drawDegreeCValue(dewPoint, 124, 63);

    u8g2.sendBuffer();
    lastReadTime = now;
  }
  for (int i = 0; i < 3; i++)
  {
    int buttonState = digitalRead(btns[i].pin);
    if (buttonState != btns[i].state)
    {
      unsigned long currentTime = millis();
      if (currentTime - btns[i].lastPressTime > DEBOUNCE_DELAY)
      {
        btns[i].state = buttonState;
        btns[i].lastPressTime = currentTime;
        
        if (buttonState == LOW)
        {
          Serial.print("Button ");
          Serial.print(btns[i].name);
          Serial.println(" pressed");
        }
      }
    }
  }
}