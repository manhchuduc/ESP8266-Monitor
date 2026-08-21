#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <Arduino.h>

// Network configuration fields stored in LittleFS
struct NetConfig {
  String ssid;
  String password;
  String mqtt_server;
  String mqtt_user;
  String mqtt_pass;
  bool mqtt_toggle = false;
  int telemetry_interval = 30;
  int screen_timeout_idx = 0;
};

extern NetConfig netConfig;
extern bool apRunning;

// Load config from /config.json on LittleFS
void loadConfig();

// Save config to /config.json on LittleFS
void saveConfig();

// Disconnect STA, start AP (SSID: "ESP_Config", open), launch web server on port 80
void startAP();

// Stop web server, disable AP, reconnect STA with saved config
void stopAP();

// Must be called from loop() while AP is running to service HTTP clients
void handleAPClient();

#endif // CONFIG_PORTAL_H
