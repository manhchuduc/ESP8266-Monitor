#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <PubSubClient.h>

#include <ESP8266WiFi.h>

// Pin definitions
#define BTN_DOWN 13
#define BTN_UP 0
#define BTN_OK 2

// UI State Machine
enum UIState
{
  STATE_MAIN,
  STATE_MAIN_MENU,
  STATE_FAN_MENU,
  STATE_FAN_SPEED,
  STATE_SCREEN_TIMEOUT,
  STATE_TELEMETRY
};

// Hardware
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern PubSubClient mqttClient;

// UI State
extern UIState currentState;
extern int mainMenuIndex;
extern int fanMenuIndex;
extern int screenTimeoutIndex;

// Fan control
extern bool fanPower;
extern int fanSpeed;
extern bool fanOscillate;
extern int fanTimer;

// Toggle states
extern bool mqttToggleState;
extern bool apToggleState;

// Menu/screen timing
extern unsigned long lastInteractionTime;
extern unsigned long lastGlobalActivityTime;
extern bool isScreenOn;
extern bool forceRedraw;

// Temp adjustment variables
extern int tempFanSpeed;
extern int tempScreenTimeoutIndex;

// Telemetry
extern int telemetryInterval;
extern int tempTelemetryInterval;

// Sensor cache
extern float cachedTemp;
extern float cachedHum;
extern float cachedDewPoint;

// NTP
extern bool ntpSynced;

#endif // GLOBALS_H
