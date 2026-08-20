#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <Adafruit_AHTX0.h>
#include <time.h>
#include <PubSubClient.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif
#include "ConfigPortal.h"

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

// ======== UI State Machine ========
enum UIState
{
  STATE_MAIN,
  STATE_MAIN_MENU,
  STATE_FAN_MENU,
  STATE_FAN_SPEED
};

UIState currentState = STATE_MAIN;
int mainMenuIndex = 0;
int fanMenuIndex = 0;

// Fan control state
bool fanPower = false;
int fanSpeed = 1; // 1-3
bool fanOscillate = false;
int fanTimer = 0; // minutes, 0 = off

// MQTT toggle state
bool mqttToggleState = false;

// AP toggle state
bool apToggleState = false;

// Menu timeout
unsigned long lastInteractionTime = 0;
const unsigned long MENU_TIMEOUT = 5000;

// ======== Button System with Double Click ========
struct Button
{
  const uint8_t pin;
  const char *name;
  bool state;
  unsigned long lastPressTime;
  bool pressed; // single-shot flag for this loop iteration
};
const unsigned long DEBOUNCE_DELAY = 80;

Button btns[] = {
    {BTN_DOWN, "DOWN", HIGH, 0, false},
    {BTN_UP, "UP", HIGH, 0, false},
    {BTN_OK, "OK", HIGH, 0, false}};

// Double-click detection for OK button
unsigned long okFirstPressTime = 0;
bool okWaitingSecondPress = false;
bool okDoubleClicked = false;
bool okSingleClicked = false;
const unsigned long DOUBLE_CLICK_WINDOW = 300;

// Temporary variable for fan speed adjustment
int tempFanSpeed = 1;

// Flag to force a redraw
bool forceRedraw = true;

// ======== Utility Functions ========

float calculateDewPoint(float t, float h)
{
  float temp = (17.271 * t) / (237.7 + t) + log(h / 100.0);
  return (237.7 * temp) / (17.271 - temp);
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
  int circleY = y - 10;
  u8g2.drawCircle(circleX, circleY, 2);
  u8g2.setCursor(startX + u8g2.getUTF8Width(valStr.c_str()) + circleSpace, y);
  u8g2.print(cStr);
}

// WiFi & MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
const int mqttPort = 1883;

// MQTT reconnect timing
unsigned long lastMqttReconnect = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // 5 seconds

// NTP time sync flag
bool ntpSynced = false;
bool ntpConfigured = false;

// ======== Draw Functions ========

void drawStatusBar()
{
  // --- Determine real WiFi state ---
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  int wifiStrength = 0; // 0-4 bars based on RSSI
  if (wifiConnected)
  {
    int rssi = WiFi.RSSI();
    if (rssi >= -55)
      wifiStrength = 4;
    else if (rssi >= -67)
      wifiStrength = 3;
    else if (rssi >= -75)
      wifiStrength = 2;
    else if (rssi >= -85)
      wifiStrength = 1;
    else
      wifiStrength = 0;
  }

  // --- WiFi icon (góc trên trái) ---
  // Vẽ các vạch sóng từ thấp đến cao
  int barX = 2;
  int barBottomY = 10;
  for (int i = 0; i < 4; i++)
  {
    int barHeight = 3 + i * 2; // cao dần: 3, 5, 7, 9
    int barY = barBottomY - barHeight;
    if (wifiConnected && i < wifiStrength)
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
  if (mqttClient.connected())
  {
    u8g2.drawDisc(mqttX + 3, 6, 3); // chấm tròn đặc = connected
  }
  else
  {
    u8g2.drawCircle(mqttX + 3, 6, 3); // chấm tròn rỗng = disconnected
  }

  // --- Time (góc trên phải) ---
  u8g2.setFont(u8g2_font_6x12_tr); // font nhỏ cho status bar
  char timeStr[6] = "--:--";
  if (ntpSynced)
  {
    time_t now = time(nullptr);
    struct tm *timeInfo = localtime(&now);
    if (timeInfo && timeInfo->tm_year > (2020 - 1900))
    {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
    }
  }
  int timeWidth = u8g2.getStrWidth(timeStr);
  u8g2.setCursor(126 - timeWidth, 10);
  u8g2.print(timeStr);

  // --- Đường kẻ phân tách ---
  u8g2.drawHLine(0, 13, 128);

  // Khôi phục font cho nội dung chính
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}

// Vẽ một hàng menu (có highlight nếu đang chọn)
// rowIndex: 0, 1, 2 (vị trí dòng trên màn hình)
void drawMenuRow(int rowIndex, const char *label, const char *rightText, bool selected)
{
  const int yPositions[] = {28, 46, 63};
  const int separatorY[] = {32, 50, -1}; // không vẽ separator ở dòng cuối
  int y = yPositions[rowIndex];

  if (selected)
  {
    // Bôi đen: nền trắng, chữ đen
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, y - 14, 128, 17);
    u8g2.setDrawColor(0);
  }

  u8g2.setCursor(4, y);
  u8g2.print(label);

  if (rightText != nullptr && rightText[0] != '\0')
  {
    int rw = u8g2.getUTF8Width(rightText);
    u8g2.setCursor(124 - rw, y);
    u8g2.print(rightText);
  }

  // Khôi phục draw color
  u8g2.setDrawColor(1);

  // Vẽ separator (trừ dòng cuối)
  if (rowIndex < 2 && separatorY[rowIndex] > 0)
  {
    u8g2.drawHLine(4, separatorY[rowIndex], 120);
  }
}

void drawMainScreen(float temp, float hum, float dewPoint)
{
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
}

void drawMainMenu()
{
  // Main Menu: 3 items
  // 0: Đ.Khiển Quạt
  // 1: Bật/Tắt MQTT
  // 2: AP
  const char *items[] = {"Đ.Khiển Quạt", "MQTT", "AP"};
  const int itemCount = 3;

  const char *mqttStatus = mqttToggleState ? "Bật" : "Tắt";
  const char *apStatus = apToggleState ? "Bật" : "Tắt";
  const char *rightTexts[] = {"", mqttStatus, apStatus};

  for (int row = 0; row < 3 && row < itemCount; row++)
  {
    drawMenuRow(row, items[row], rightTexts[row], row == mainMenuIndex);
  }
}

void drawFanMenu()
{
  // Fan Menu: 4 items with scrolling
  // 0: Bật/Tắt     [Bật/Tắt]
  // 1: Chỉnh số     [1-3]
  // 2: Quay          [Bật/Tắt]
  // 3: Hẹn giờ       [60p / ""]
  const char *items[] = {"1. Bật/Tắt", "2. Chỉnh số", "3. Quay", "4. Hẹn giờ"};
  const int itemCount = 4;

  // Build right-side text
  const char *fanPowerStr = fanPower ? "Bật" : "Tắt";
  String fanSpeedStr = String(fanSpeed);
  const char *fanOscStr = fanOscillate ? "Bật" : "Tắt";
  String timerStr = "";
  if (fanTimer > 0)
  {
    timerStr = String(fanTimer) + "p";
  }

  const char *rightTexts[4];
  rightTexts[0] = fanPowerStr;
  rightTexts[1] = fanSpeedStr.c_str();
  rightTexts[2] = fanOscStr;
  rightTexts[3] = timerStr.c_str();

  // Scrolling: determine which 3 items to show
  // scrollOffset is the index of the first visible item
  int scrollOffset = 0;
  if (fanMenuIndex >= 3)
  {
    scrollOffset = fanMenuIndex - 2; // keep selected item on last row
  }
  else if (fanMenuIndex >= 2 && itemCount > 3)
  {
    scrollOffset = fanMenuIndex - 2;
  }

  // Clamp scrollOffset
  if (scrollOffset > itemCount - 3)
    scrollOffset = itemCount - 3;
  if (scrollOffset < 0)
    scrollOffset = 0;

  for (int row = 0; row < 3; row++)
  {
    int itemIdx = scrollOffset + row;
    if (itemIdx < itemCount)
    {
      drawMenuRow(row, items[itemIdx], rightTexts[itemIdx], itemIdx == fanMenuIndex);
    }
  }

  // Draw scroll indicator if there are items above/below
  if (scrollOffset > 0)
  {
    // Up arrow indicator at top-right
    u8g2.drawTriangle(120, 18, 124, 18, 122, 15);
  }
  if (scrollOffset + 3 < itemCount)
  {
    // Down arrow indicator at bottom-right
    u8g2.drawTriangle(120, 61, 124, 61, 122, 64);
  }
}

void drawFanSpeedAdjust()
{
  // Draw a centered overlay box showing the current fan speed
  int boxW = 60;
  int boxH = 40;
  int boxX = (128 - boxW) / 2;
  int boxY = (64 - boxH) / 2;

  // Clear the box area
  u8g2.setDrawColor(0);
  u8g2.drawBox(boxX + 1, boxY + 1, boxW - 2, boxH - 2);
  u8g2.setDrawColor(1);

  // Draw border
  u8g2.drawFrame(boxX, boxY, boxW, boxH);

  // Draw title
  u8g2.setFont(u8g2_font_6x12_tr);
  const char *title = "Toc do";
  int titleW = u8g2.getStrWidth(title);
  u8g2.setCursor(boxX + (boxW - titleW) / 2, boxY + 12);
  u8g2.print(title);

  // Draw the speed number large
  u8g2.setFont(u8g2_font_logisoso16_tn); // large number font
  String speedStr = String(tempFanSpeed);
  int numW = u8g2.getStrWidth(speedStr.c_str());
  u8g2.setCursor(boxX + (boxW - numW) / 2, boxY + 34);
  u8g2.print(speedStr);

  // Restore font
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}

// ======== Button Processing ========

void processButtons()
{
  // Read and debounce all buttons
  for (int i = 0; i < 3; i++)
  {
    btns[i].pressed = false;
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
          btns[i].pressed = true;
          Serial.print("Button ");
          Serial.print(btns[i].name);
          Serial.println(" pressed");
        }
      }
    }
  }

  // ---- Double-click detection for OK ----
  okDoubleClicked = false;
  okSingleClicked = false;
  unsigned long now = millis();

  if (btns[2].pressed) // OK pressed
  {
    if (okWaitingSecondPress && (now - okFirstPressTime < DOUBLE_CLICK_WINDOW))
    {
      // Second press within window -> double click
      okDoubleClicked = true;
      okWaitingSecondPress = false;
    }
    else
    {
      // First press or too late -> start new window
      okFirstPressTime = now;
      okWaitingSecondPress = true;
    }
  }

  // Check if single-click window expired
  if (okWaitingSecondPress && (now - okFirstPressTime >= DOUBLE_CLICK_WINDOW))
  {
    okSingleClicked = true;
    okWaitingSecondPress = false;
  }
}

void handleInput()
{
  bool upPressed = btns[1].pressed;
  bool downPressed = btns[0].pressed;

  // Any button press in menu states resets timeout
  if (currentState != STATE_MAIN)
  {
    if (upPressed || downPressed || okSingleClicked || okDoubleClicked)
    {
      lastInteractionTime = millis();
      forceRedraw = true;
    }
  }

  switch (currentState)
  {
  case STATE_MAIN:
    if (okDoubleClicked)
    {
      currentState = STATE_MAIN_MENU;
      mainMenuIndex = 0;
      lastInteractionTime = millis();
      forceRedraw = true;
    }
    break;

  case STATE_MAIN_MENU:
    if (upPressed)
    {
      if (mainMenuIndex > 0)
        mainMenuIndex--;
    }
    if (downPressed)
    {
      if (mainMenuIndex < 2) // 3 items: 0, 1, 2
        mainMenuIndex++;
    }
    if (okSingleClicked)
    {
      if (mainMenuIndex == 0)
      {
        // Đ.Khiển Quạt -> go to Fan Menu
        currentState = STATE_FAN_MENU;
        fanMenuIndex = 0;
      }
      else if (mainMenuIndex == 1)
      {
        // Bật/Tắt MQTT -> toggle
        mqttToggleState = !mqttToggleState;
      }
      else if (mainMenuIndex == 2)
      {
        // AP -> toggle
        apToggleState = !apToggleState;
        if (apToggleState)
        {
          startAP();
        }
        else
        {
          stopAP();
        }
      }
    }
    if (okDoubleClicked)
    {
      // Double click in menu also goes back to main
      currentState = STATE_MAIN;
      forceRedraw = true;
    }
    break;

  case STATE_FAN_MENU:
    if (upPressed)
    {
      if (fanMenuIndex > 0)
        fanMenuIndex--;
    }
    if (downPressed)
    {
      if (fanMenuIndex < 3) // 4 items: 0-3
        fanMenuIndex++;
    }
    if (okSingleClicked)
    {
      switch (fanMenuIndex)
      {
      case 0: // Bật/Tắt
        fanPower = !fanPower;
        break;
      case 1: // Chỉnh số -> open speed adjust overlay
        tempFanSpeed = fanSpeed;
        currentState = STATE_FAN_SPEED;
        break;
      case 2: // Quay
        fanOscillate = !fanOscillate;
        break;
      case 3: // Hẹn giờ -> cycle through preset times
        // Cycle: 0 -> 30 -> 60 -> 120 -> 0
        if (fanTimer == 0)
          fanTimer = 30;
        else if (fanTimer == 30)
          fanTimer = 60;
        else if (fanTimer == 60)
          fanTimer = 120;
        else
          fanTimer = 0;
        break;
      }
    }
    if (okDoubleClicked)
    {
      // Double click goes back to main menu
      currentState = STATE_MAIN_MENU;
      mainMenuIndex = 0;
      forceRedraw = true;
    }
    break;

  case STATE_FAN_SPEED:
    if (upPressed)
    {
      if (tempFanSpeed < 3)
        tempFanSpeed++;
    }
    if (downPressed)
    {
      if (tempFanSpeed > 1)
        tempFanSpeed--;
    }
    if (okSingleClicked)
    {
      // Confirm speed
      fanSpeed = tempFanSpeed;
      currentState = STATE_FAN_MENU;
      forceRedraw = true;
    }
    if (okDoubleClicked)
    {
      // Cancel and go back
      currentState = STATE_FAN_MENU;
      forceRedraw = true;
    }
    break;
  }
}

// ======== Setup ========

void setup()
{
  Serial.begin(115200);

  // Load saved network config from LittleFS
  loadConfig();

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

  // --- Connect WiFi STA if credentials exist ---
  if (netConfig.ssid.length() > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(netConfig.ssid.c_str(), netConfig.password.c_str());
    Serial.print("[WiFi] Connecting to ");
    Serial.println(netConfig.ssid);
  }

  // --- Configure MQTT client ---
  if (netConfig.mqtt_server.length() > 0)
  {
    mqttClient.setServer(netConfig.mqtt_server.c_str(), mqttPort);
    Serial.print("[MQTT] Server set to ");
    Serial.println(netConfig.mqtt_server);
  }
}

// ======== Cached sensor values ========
float cachedTemp = 0;
float cachedHum = 0;
float cachedDewPoint = 0;

// ======== Main Loop ========

void loop()
{
  unsigned long now = millis();

  // --- Check NTP sync status ---
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0)
  {
    if (!ntpConfigured)
    {
      configTime(7 * 3600, 0, "vn.pool.ntp.org", "time.google.com", "pool.ntp.org");
      ntpConfigured = true;
      Serial.println("[NTP] Configured NTP");
    }

    if (!ntpSynced)
    {

      time_t t = time(nullptr);
      struct tm *ti = localtime(&t);
      if (ti && ti->tm_year > (2020 - 1900))
      {
        ntpSynced = true;
        Serial.println("[NTP] Time synced");
        forceRedraw = true; // Force UI update when synced
      }
    }
  }

  // --- MQTT reconnect ---
  if (mqttToggleState && WiFi.status() == WL_CONNECTED && !mqttClient.connected())
  {
    if (now - lastMqttReconnect >= MQTT_RECONNECT_INTERVAL)
    {
      lastMqttReconnect = now;
      Serial.println("[MQTT] Attempting connection...");
#if defined(ESP8266)
      String clientId = "ESP_" + String(ESP.getChipId(), HEX);
#elif defined(ESP32)
      String clientId = "ESP_" + String((uint32_t)ESP.getEfuseMac(), HEX);
#endif
      bool connected;
      if (netConfig.mqtt_user.length() > 0)
      {
        connected = mqttClient.connect(clientId.c_str(),
                                       netConfig.mqtt_user.c_str(),
                                       netConfig.mqtt_pass.c_str());
      }
      else
      {
        connected = mqttClient.connect(clientId.c_str());
      }
      if (connected)
      {
        Serial.println("[MQTT] Connected");
      }
      else
      {
        Serial.print("[MQTT] Failed, rc=");
        Serial.println(mqttClient.state());
      }
    }
  }

  // --- Service MQTT client ---
  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // --- Read sensor periodically ---
  if (now - lastReadTime >= 1000)
  {
    sensors_event_t humidity, temp_event;
    aht.getEvent(&humidity, &temp_event);

    cachedTemp = temp_event.temperature;
    cachedHum = humidity.relative_humidity;
    cachedDewPoint = calculateDewPoint(cachedTemp, cachedHum);

    lastReadTime = now;

    // Main screen redraws every second with new sensor data
    if (currentState == STATE_MAIN)
    {
      forceRedraw = true;
    }
  }

  // --- Process buttons ---
  processButtons();
  handleInput();

  // --- Service AP web server if running ---
  handleAPClient();

  // --- Menu timeout ---
  // Re-read millis() to avoid unsigned underflow: handleInput() may have set
  // lastInteractionTime to a millis() value newer than the `now` captured above.
  now = millis();
  if (currentState != STATE_MAIN)
  {
    if (now - lastInteractionTime > MENU_TIMEOUT)
    {
      currentState = STATE_MAIN;
      forceRedraw = true;
    }
  }

  // --- Render ---
  if (forceRedraw)
  {
    forceRedraw = false;

    u8g2.clearBuffer();
    drawStatusBar();

    switch (currentState)
    {
    case STATE_MAIN:
      drawMainScreen(cachedTemp, cachedHum, cachedDewPoint);
      break;
    case STATE_MAIN_MENU:
      drawMainMenu();
      break;
    case STATE_FAN_MENU:
      drawFanMenu();
      break;
    case STATE_FAN_SPEED:
      // Draw fan menu underneath, then overlay
      drawFanMenu();
      drawFanSpeedAdjust();
      break;
    }

    u8g2.sendBuffer();
  }
}