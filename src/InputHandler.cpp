#include "InputHandler.h"
#include "MqttManager.h"
#include "ConfigPortal.h"
#include "Globals.h"

// ======== Button System with Double Click ========
struct Button
{
  const uint8_t pin;
  const char *name;
  bool state;
  bool lastReading;
  unsigned long lastDebounceTime;
  bool pressed; // single-shot flag for this loop iteration
};
static const unsigned long DEBOUNCE_DELAY = 50;

static Button btns[] = {
    {BTN_DOWN, "DOWN", HIGH, HIGH, 0, false},
    {BTN_UP, "UP", HIGH, HIGH, 0, false},
    {BTN_OK, "OK", HIGH, HIGH, 0, false}};

// Double-click detection for OK button
static unsigned long okFirstPressTime = 0;
static bool okWaitingSecondPress = false;
static bool okDoubleClicked = false;
static bool okSingleClicked = false;
static const unsigned long DOUBLE_CLICK_WINDOW = 300;

void initButtons()
{
  for (int i = 0; i < 3; i++)
  {
    pinMode(btns[i].pin, INPUT_PULLUP);
  }
}

void processButtons()
{
  unsigned long currentTime = millis();

  // Read and debounce all buttons
  for (int i = 0; i < 3; i++)
  {
    btns[i].pressed = false;
    bool currentReading = digitalRead(btns[i].pin);

    // Reset the debounce timer if the pin state changed (due to noise or press)
    if (currentReading != btns[i].lastReading)
    {
      btns[i].lastDebounceTime = currentTime;
    }

    // If the reading has been physically stable for longer than the debounce delay
    if ((currentTime - btns[i].lastDebounceTime) > DEBOUNCE_DELAY)
    {
      // If the button state has changed
      if (currentReading != btns[i].state)
      {
        btns[i].state = currentReading;

        // Only trigger 'pressed' when transitioning to LOW
        if (btns[i].state == LOW)
        {
          btns[i].pressed = true;
          Serial.print("Button ");
          Serial.print(btns[i].name);
          Serial.println(" pressed");
        }
      }
    }
    btns[i].lastReading = currentReading;
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
  bool okPressed = btns[2].pressed;
  bool anyAction = upPressed || downPressed || okPressed || okSingleClicked || okDoubleClicked;

  if (anyAction)
  {
    lastGlobalActivityTime = millis();
    if (!isScreenOn)
    {
      isScreenOn = true;
      u8g2.setPowerSave(0);
      forceRedraw = true;

      // If we are waking up the screen, we also want to reset double-click states
      // so it doesn't trigger a click after the window expires
      okWaitingSecondPress = false;

      return; // Consume the button press to just wake up
    }
  }

  // Any button press in menu states resets timeout
  if (currentState != STATE_MAIN)
  {
    if (anyAction)
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
      if (mainMenuIndex < 4) // 5 items: 0, 1, 2, 3, 4
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
        // Tắt màn
        tempScreenTimeoutIndex = screenTimeoutIndex;
        currentState = STATE_SCREEN_TIMEOUT;
      }
      else if (mainMenuIndex == 2)
      {
        // Bật/Tắt MQTT -> toggle
        mqttToggleState = !mqttToggleState;
        if (!mqttToggleState && mqttClient.connected())
        {
          mqttClient.disconnect();
        }
        netConfig.mqtt_toggle = mqttToggleState;
        saveConfig();
      }
      else if (mainMenuIndex == 3)
      {
        // Chu kỳ gửi -> open telemetry adjust overlay
        tempTelemetryInterval = telemetryInterval;
        currentState = STATE_TELEMETRY;
      }
      else if (mainMenuIndex == 4)
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
        publishFanState();
        break;
      case 1: // Chỉnh số -> open speed adjust overlay
        if (fanPower) {
          tempFanSpeed = fanSpeed;
          currentState = STATE_FAN_SPEED;
        }
        break;
      case 2: // Quay
        if (fanPower) {
          fanOscillate = !fanOscillate;
          publishFanState();
        }
        break;
      case 3: // Hẹn giờ -> open timer adjust overlay
        if (fanPower) {
          tempFanTimerHour = fanTimer / 60;
          tempFanTimerMinute = fanTimer % 60;
          currentState = STATE_FAN_TIMER_HOUR;
        }
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
      publishFanState();
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

  case STATE_SCREEN_TIMEOUT:
    if (upPressed)
    {
      if (tempScreenTimeoutIndex < 6)
        tempScreenTimeoutIndex++;
    }
    if (downPressed)
    {
      if (tempScreenTimeoutIndex > 0)
        tempScreenTimeoutIndex--;
    }
    if (okSingleClicked)
    {
      screenTimeoutIndex = tempScreenTimeoutIndex;
      netConfig.screen_timeout_idx = screenTimeoutIndex;
      saveConfig();
      currentState = STATE_MAIN_MENU;
      forceRedraw = true;
    }
    if (okDoubleClicked)
    {
      currentState = STATE_MAIN_MENU;
      forceRedraw = true;
    }
    break;

  case STATE_TELEMETRY:
    if (upPressed)
    {
      if (tempTelemetryInterval < 60)
        tempTelemetryInterval += 5;
    }
    if (downPressed)
    {
      if (tempTelemetryInterval > 5)
        tempTelemetryInterval -= 5;
    }
    if (okSingleClicked)
    {
      telemetryInterval = tempTelemetryInterval;
      netConfig.telemetry_interval = telemetryInterval;
      saveConfig();
      currentState = STATE_MAIN_MENU;
      forceRedraw = true;
    }
    if (okDoubleClicked)
    {
      currentState = STATE_MAIN_MENU;
      forceRedraw = true;
    }
    break;

  case STATE_FAN_TIMER_HOUR:
    if (upPressed)
    {
      if (tempFanTimerHour < 15)
        tempFanTimerHour++;
    }
    if (downPressed)
    {
      if (tempFanTimerHour > 0)
        tempFanTimerHour--;
    }
    if (okSingleClicked)
    {
      // Confirm hour, move to minute adjustment
      currentState = STATE_FAN_TIMER_MINUTE;
      forceRedraw = true;
    }
    if (okDoubleClicked)
    {
      // Cancel and go back
      currentState = STATE_FAN_MENU;
      forceRedraw = true;
    }
    break;

  case STATE_FAN_TIMER_MINUTE:
    if (upPressed)
    {
      tempFanTimerMinute += 15;
      if (tempFanTimerMinute > 45)
        tempFanTimerMinute = 0;
    }
    if (downPressed)
    {
      tempFanTimerMinute -= 15;
      if (tempFanTimerMinute < 0)
        tempFanTimerMinute = 45;
    }
    if (okSingleClicked)
    {
      // Save total minutes and publish
      fanTimer = tempFanTimerHour * 60 + tempFanTimerMinute;
      publishFanState();
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
