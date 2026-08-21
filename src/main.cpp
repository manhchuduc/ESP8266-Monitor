#include "MqttManager.h"
#include "Display.h"
#include "InputHandler.h"
#include "ConfigPortal.h"
#include <Wire.h>
#include <math.h>
#include <Adafruit_AHTX0.h>
#include <time.h>
#include "Globals.h"

// ======== Hardware ========
Adafruit_AHTX0 aht;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ======== WiFi & MQTT ========
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
const int mqttPort = 1883;
unsigned long lastMqttReconnect = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // 5 seconds

// ======== UI State ========
UIState currentState = STATE_MAIN;
int mainMenuIndex = 0;
int fanMenuIndex = 0;
int screenTimeoutIndex = 0;

// ======== Fan Control ========
bool fanPower = false;
int fanSpeed = 1; // 1-3
bool fanOscillate = false;
int fanTimer = 0; // minutes, 0 = off

// ======== Toggle States ========
bool mqttToggleState = false;
bool apToggleState = false;

// ======== Timing ========
unsigned long lastInteractionTime = 0;
const unsigned long MENU_TIMEOUT = 5000;
const unsigned long OVERLAY_TIMEOUT = 3000;
unsigned long lastGlobalActivityTime = 0;
unsigned long lastReadTime = 0;
unsigned long lastTelemetryTime = 0;

// ======== Screen ========
bool isScreenOn = true;
bool forceRedraw = true;

// ======== MQTT Dynamic Subscription ========
bool isMqttListening = false;
unsigned long lastFanMenuTime = 0;

// ======== Temp Adjustment ========
int tempFanSpeed = 1;
int tempScreenTimeoutIndex = 0;
int tempFanTimerHour = 0;
int tempFanTimerMinute = 0;

// ======== Telemetry ========
int telemetryInterval = 30;
int tempTelemetryInterval = 30;

// ======== Sensor Cache ========
float cachedTemp = 0;
float cachedHum = 0;
float cachedDewPoint = 0;

// ======== NTP ========
bool ntpSynced = false;
bool ntpConfigured = false;

// ======== Utility Functions ========

float calculateDewPoint(float t, float h)
{
  float temp = (17.271 * t) / (237.7 + t) + log(h / 100.0);
  return (237.7 * temp) / (17.271 - temp);
}

// ======== Setup ========

void setup()
{
  Serial.begin(115200);

  // Initialize activity time
  lastGlobalActivityTime = millis();

  // Load saved network config from LittleFS
  loadConfig();
  mqttToggleState = netConfig.mqtt_toggle;
  telemetryInterval = netConfig.telemetry_interval;
  screenTimeoutIndex = netConfig.screen_timeout_idx;

  // Initialize buttons
  initButtons();

  if (!aht.begin())
  {
    Serial.println("Could not find AHT? Check wiring");
  }

  u8g2.begin();
  u8g2.enableUTF8Print();

  // --- Initialize MQTT topics ---
  initMqttTopics();

  // --- Connect WiFi STA if credentials exist ---
  if (netConfig.ssid.length() > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
    WiFi.begin(netConfig.ssid.c_str(), netConfig.password.c_str());
    Serial.print("[WiFi] Connecting to ");
    Serial.println(netConfig.ssid);
  }

  // --- Configure MQTT client ---
  if (netConfig.mqtt_server.length() > 0)
  {
    mqttClient.setServer(netConfig.mqtt_server.c_str(), mqttPort);
    mqttClient.setBufferSize(1024);
    mqttClient.setCallback(mqttCallback);
    Serial.print("[MQTT] Server set to ");
    Serial.println(netConfig.mqtt_server);
  }
}

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
  if (!mqttClient.connected())
  {
    isMqttListening = false;
  }

  if (mqttToggleState && WiFi.status() == WL_CONNECTED && !mqttClient.connected())
  {
    if (now - lastMqttReconnect >= MQTT_RECONNECT_INTERVAL)
    {
      lastMqttReconnect = now;
      Serial.println("[MQTT] Attempting connection...");
      bool connected;
      if (netConfig.mqtt_user.length() > 0)
      {
        connected = mqttClient.connect(mqttClientId.c_str(),
                                       netConfig.mqtt_user.c_str(),
                                       netConfig.mqtt_pass.c_str());
      }
      else
      {
        connected = mqttClient.connect(mqttClientId.c_str());
      }
      if (connected)
      {
        Serial.println("[MQTT] Connected");
        // Publish Home Assistant Auto Discovery configs
        publishHADiscovery();
        // Request state sync from Home Assistant
        requestSyncState();
      }
      else
      {
        Serial.print("[MQTT] Failed, rc=");
        Serial.println(mqttClient.state());
      }
    }
  }

  // --- Service MQTT client & telemetry ---
  if (mqttClient.connected())
  {
    mqttClient.loop();

    // Periodic telemetry publish
    if (now - lastTelemetryTime >= (unsigned long)telemetryInterval * 1000)
    {
      publishSensorData();
      lastTelemetryTime = now;
    }
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

  // --- Menu / Overlay timeout ---
  // Re-read millis() to avoid unsigned underflow: handleInput() may have set
  // lastInteractionTime to a millis() value newer than the `now` captured above.
  now = millis();
  if (currentState != STATE_MAIN)
  {
    // Overlay screens: 3-second timeout -> cancel edits, return to parent menu
    if (currentState == STATE_FAN_SPEED ||
        currentState == STATE_FAN_TIMER_HOUR ||
        currentState == STATE_FAN_TIMER_MINUTE)
    {
      if (now - lastInteractionTime > OVERLAY_TIMEOUT)
      {
        currentState = STATE_FAN_MENU;
        forceRedraw = true;
      }
    }
    else if (currentState == STATE_SCREEN_TIMEOUT ||
             currentState == STATE_TELEMETRY)
    {
      if (now - lastInteractionTime > OVERLAY_TIMEOUT)
      {
        currentState = STATE_MAIN_MENU;
        forceRedraw = true;
      }
    }
    // Regular menu screens: 5-second timeout -> return to main screen
    else
    {
      if (now - lastInteractionTime > MENU_TIMEOUT)
      {
        currentState = STATE_MAIN;
        forceRedraw = true;
      }
    }
  }

  // --- Screen timeout ---
  if (screenTimeoutIndex > 0 && isScreenOn)
  {
    unsigned long timeoutMs = screenTimeoutIndex * 5000;
    if (now - lastGlobalActivityTime > timeoutMs)
    {
      isScreenOn = false;
      u8g2.setPowerSave(1);
      currentState = STATE_MAIN; // Return to main screen when screen turns off
    }
  }

  // --- MQTT Dynamic Subscription ---
  if (mqttClient.connected())
  {
    bool inFanMenu = (currentState == STATE_FAN_MENU ||
                      currentState == STATE_FAN_SPEED ||
                      currentState == STATE_FAN_TIMER_HOUR ||
                      currentState == STATE_FAN_TIMER_MINUTE);

    if (inFanMenu)
    {
      lastFanMenuTime = millis();
      if (!isMqttListening)
      {
        mqttClient.subscribe(topicCmnd.c_str());
        Serial.println("[MQTT] Subscribed to commands");
        isMqttListening = true;
      }
    }
    else
    {
      if (isMqttListening && (millis() - lastFanMenuTime > 5000))
      {
        mqttClient.unsubscribe(topicCmnd.c_str());
        Serial.println("[MQTT] Unsubscribed from commands");
        isMqttListening = false;
      }
    }
  }

  // --- Render ---
  if (forceRedraw && isScreenOn)
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
    case STATE_SCREEN_TIMEOUT:
      drawMainMenu();
      drawScreenTimeoutAdjust();
      break;
    case STATE_TELEMETRY:
      drawMainMenu();
      drawTelemetryAdjust();
      break;
    case STATE_FAN_TIMER_HOUR:
    case STATE_FAN_TIMER_MINUTE:
      drawFanMenu();
      drawFanTimerAdjust();
      break;
    }

    u8g2.sendBuffer();
  }

  // Yield to RTOS for power saving (Dynamic Sleep)
  delay(10);
}