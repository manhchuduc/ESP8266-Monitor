#include "Display.h"
#include <time.h>
#include "Globals.h"

// ======== Utility Functions ========

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
      strftime(timeStr, sizeof(timeStr), "%H:%M", timeInfo);
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
  // Main Menu: 5 items
  // 0: Đ.Khiển Quạt
  // 1: Tắt màn
  // 2: MQTT
  // 3: Chu kỳ gửi
  // 4: AP
  const char *items[] = {"1. Đ.Khiển Quạt", "2. Tắt màn", "3. MQTT", "4. C.kỳ gửi", "5. AP"};
  const int itemCount = 5;

  const char *mqttStatus = mqttToggleState ? "Bật" : "Tắt";
  const char *apStatus = apToggleState ? "Bật" : "Tắt";

  const char *screenTimeoutOptions[] = {"Ko", "5s", "10s", "15s", "20s", "25s", "30s"};
  const char *screenTimeoutStatus = screenTimeoutOptions[screenTimeoutIndex];

  // Telemetry interval display string
  String telemetryStr = String(telemetryInterval) + "s";

  const char *rightTexts[] = {"", screenTimeoutStatus, mqttStatus, telemetryStr.c_str(), apStatus};

  // Scrolling logic
  int scrollOffset = 0;
  if (mainMenuIndex >= 3)
  {
    scrollOffset = mainMenuIndex - 2;
  }
  else if (mainMenuIndex >= 2 && itemCount > 3)
  {
    scrollOffset = mainMenuIndex - 2;
  }

  if (scrollOffset > itemCount - 3)
    scrollOffset = itemCount - 3;
  if (scrollOffset < 0)
    scrollOffset = 0;

  for (int row = 0; row < 3; row++)
  {
    int itemIdx = scrollOffset + row;
    if (itemIdx < itemCount)
    {
      drawMenuRow(row, items[itemIdx], rightTexts[itemIdx], itemIdx == mainMenuIndex);
    }
  }

  // Draw scroll indicator
  if (scrollOffset > 0)
  {
    u8g2.drawTriangle(120, 18, 124, 18, 122, 15);
  }
  if (scrollOffset + 3 < itemCount)
  {
    u8g2.drawTriangle(120, 61, 124, 61, 122, 64);
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
  String timerStr = "Ko";
  if (fanTimer > 0 && ntpSynced && fanTimer > time(nullptr))
  {
    timerStr = "";
    int remainingMin = (fanTimer - time(nullptr)) / 60;
    int h = remainingMin / 60;
    int m = remainingMin % 60;
    if (h > 0)
      timerStr += String(h) + "h";
    if (m > 0)
      timerStr += String(m) + "p";
    
    // Nếu cả h và m đều 0 (còn dưới 1 phút)
    if (h == 0 && m == 0)
      timerStr = "0p";
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

void drawScreenTimeoutAdjust()
{
  int boxW = 60;
  int boxH = 40;
  int boxX = (128 - boxW) / 2;
  int boxY = (64 - boxH) / 2;

  u8g2.setDrawColor(0);
  u8g2.drawBox(boxX + 1, boxY + 1, boxW - 2, boxH - 2);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(boxX, boxY, boxW, boxH);

  u8g2.setFont(u8g2_font_6x12_tr);
  const char *title = "Tat man";
  int titleW = u8g2.getStrWidth(title);
  u8g2.setCursor(boxX + (boxW - titleW) / 2, boxY + 12);
  u8g2.print(title);

  // Draw value
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);

  const char *options[] = {"Ko", "5s", "10s", "15s", "20s", "25s", "30s"};
  const char *valStr = options[tempScreenTimeoutIndex];

  int valW = u8g2.getUTF8Width(valStr);
  u8g2.setCursor(boxX + (boxW - valW) / 2, boxY + 32);
  u8g2.print(valStr);

  // Restore font
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}

void drawTelemetryAdjust()
{
  int boxW = 60;
  int boxH = 40;
  int boxX = (128 - boxW) / 2;
  int boxY = (64 - boxH) / 2;

  u8g2.setDrawColor(0);
  u8g2.drawBox(boxX + 1, boxY + 1, boxW - 2, boxH - 2);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(boxX, boxY, boxW, boxH);

  u8g2.setFont(u8g2_font_6x12_tr);
  const char *title = "Chu ky";
  int titleW = u8g2.getStrWidth(title);
  u8g2.setCursor(boxX + (boxW - titleW) / 2, boxY + 12);
  u8g2.print(title);

  // Draw value (e.g. "30s")
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
  String valStr = String(tempTelemetryInterval) + "s";
  int valW = u8g2.getUTF8Width(valStr.c_str());
  u8g2.setCursor(boxX + (boxW - valW) / 2, boxY + 32);
  u8g2.print(valStr);

  // Restore font
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}

void drawFanTimerAdjust()
{
  int boxW = 80;
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
  const char *title = "Hen gio";
  int titleW = u8g2.getStrWidth(title);
  u8g2.setCursor(boxX + (boxW - titleW) / 2, boxY + 12);
  u8g2.print(title);

  // Draw value with active field highlighted
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);

  String timerValStr;
  if (currentState == STATE_FAN_TIMER_HOUR)
  {
    // Highlight hour: >Xh< Yp
    timerValStr = ">" + String(tempFanTimerHour) + "h< " + String(tempFanTimerMinute) + "p";
  }
  else
  {
    // Highlight minute: Xh >Yp<
    timerValStr = String(tempFanTimerHour) + "h >" + String(tempFanTimerMinute) + "p<";
  }

  int timerValW = u8g2.getUTF8Width(timerValStr.c_str());
  u8g2.setCursor(boxX + (boxW - timerValW) / 2, boxY + 32);
  u8g2.print(timerValStr);

  // Restore font
  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);
}
