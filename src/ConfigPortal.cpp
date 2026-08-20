#include "ConfigPortal.h"
#include <ArduinoJson.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <LittleFS.h>
  ESP8266WebServer server(80);
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <LittleFS.h>
  WebServer server(80);
#endif

NetConfig netConfig;
bool apRunning = false;

static const char CONFIG_FILE[] = "/config.json";

// ======== Raw HTML (copied from web/index.html) ========
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP Configuration</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #f4f4f9;
            color: #333;
            margin: 0;
            padding: 20px;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
        }
        .container {
            background-color: #fff;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            width: 100%;
            max-width: 400px;
        }
        h2 {
            margin-top: 0;
            color: #2c3e50;
            text-align: center;
        }
        .form-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: 600;
        }
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 10px;
            border: 1px solid #ccc;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 16px;
        }
        input[type="text"]:focus,
        input[type="password"]:focus {
            border-color: #3498db;
            outline: none;
        }
        .btn {
            background-color: #3498db;
            color: white;
            padding: 12px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            width: 100%;
            font-size: 16px;
            font-weight: bold;
            margin-top: 10px;
        }
        .btn:hover {
            background-color: #2980b9;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Device Configuration</h2>
        <form action="/save" method="POST">
            <div class="form-group">
                <label for="ssid">WiFi SSID</label>
                <input type="text" id="ssid" name="ssid" value="%SSID%">
            </div>
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" value="%PASS%">
            </div>
            <div class="form-group">
                <label for="mqtt_server">MQTT Server</label>
                <input type="text" id="mqtt_server" name="mqtt_server" value="%MQTT_SERVER%">
            </div>
            <div class="form-group">
                <label for="mqtt_user">MQTT Username</label>
                <input type="text" id="mqtt_user" name="mqtt_user" value="%MQTT_USER%">
            </div>
            <div class="form-group">
                <label for="mqtt_pass">MQTT Password</label>
                <input type="password" id="mqtt_pass" name="mqtt_pass" value="%MQTT_PASS%">
            </div>
            <button type="submit" class="btn">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

static const char SAVE_OK_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Saved</title>
<style>body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;background:#f4f4f9;}
.msg{background:#fff;padding:40px;border-radius:8px;box-shadow:0 4px 6px rgba(0,0,0,.1);text-align:center;}
h2{color:#27ae60;}</style></head>
<body><div class="msg"><h2>Configuration Saved!</h2><p>The device will use these settings on next WiFi connect.</p></div></body></html>
)rawliteral";

// ======== Config Persistence ========

void loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("[ConfigPortal] LittleFS mount failed");
    return;
  }

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println("[ConfigPortal] No config file found, using defaults");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("[ConfigPortal] JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  netConfig.ssid        = doc["ssid"]        | "";
  netConfig.password     = doc["password"]     | "";
  netConfig.mqtt_server  = doc["mqtt_server"]  | "";
  netConfig.mqtt_user    = doc["mqtt_user"]    | "";
  netConfig.mqtt_pass    = doc["mqtt_pass"]    | "";

  Serial.println("[ConfigPortal] Config loaded");
}

void saveConfig() {
  JsonDocument doc;
  doc["ssid"]        = netConfig.ssid;
  doc["password"]     = netConfig.password;
  doc["mqtt_server"]  = netConfig.mqtt_server;
  doc["mqtt_user"]    = netConfig.mqtt_user;
  doc["mqtt_pass"]    = netConfig.mqtt_pass;

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("[ConfigPortal] Failed to open config file for writing");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("[ConfigPortal] Config saved");
}

// ======== HTTP Handlers ========

static void handleRoot() {
  String html = FPSTR(INDEX_HTML);
  html.replace("%SSID%", netConfig.ssid);
  html.replace("%PASS%", netConfig.password);
  html.replace("%MQTT_SERVER%", netConfig.mqtt_server);
  html.replace("%MQTT_USER%", netConfig.mqtt_user);
  html.replace("%MQTT_PASS%", netConfig.mqtt_pass);
  server.send(200, "text/html", html);
}

static void handleSave() {
  netConfig.ssid        = server.arg("ssid");
  netConfig.password     = server.arg("password");
  netConfig.mqtt_server  = server.arg("mqtt_server");
  netConfig.mqtt_user    = server.arg("mqtt_user");
  netConfig.mqtt_pass    = server.arg("mqtt_pass");

  saveConfig();

  server.send_P(200, "text/html", SAVE_OK_HTML);
  Serial.println("[ConfigPortal] New config received via web");
}

// ======== AP Control ========

void startAP() {
  Serial.println("[ConfigPortal] Starting AP...");

  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP_Config");

  Serial.print("[ConfigPortal] AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  apRunning = true;
  Serial.println("[ConfigPortal] Web server started");
}

void stopAP() {
  Serial.println("[ConfigPortal] Stopping AP...");

  server.stop();
  WiFi.softAPdisconnect(true);
  delay(100);

  // Reconnect STA if credentials exist
  if (netConfig.ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(netConfig.ssid.c_str(), netConfig.password.c_str());
    Serial.print("[ConfigPortal] Reconnecting to ");
    Serial.println(netConfig.ssid);
  } else {
    WiFi.mode(WIFI_STA);
    Serial.println("[ConfigPortal] No saved SSID, staying disconnected");
  }

  apRunning = false;
  Serial.println("[ConfigPortal] AP stopped");
}

void handleAPClient() {
  if (apRunning) {
    server.handleClient();
  }
}
