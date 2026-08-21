#include "MqttManager.h"
#include "Globals.h"
#include <ArduinoJson.h>
#include <time.h>

// ======== MQTT Topics ========
String mqttClientId;
String topicSensor;         // esp_monitor/<clientId>/sensor
String topicState;          // esp_monitor/<clientId>/state
String topicSync;           // esp_monitor/<clientId>/sync
String topicCmnd;           // esp_monitor/<clientId>/command/# (subscribe)
String topicCmndPower;      // esp_monitor/<clientId>/command/power
String topicCmndFanSpeed;   // esp_monitor/<clientId>/command/fanspeed
String topicCmndOscillate;  // esp_monitor/<clientId>/command/oscillate
String topicCmndTimer;      // esp_monitor/<clientId>/command/timer
String topicCmndState;      // esp_monitor/<clientId>/command/state

// ======== Topic Initialization ========

void initMqttTopics()
{
  uint32_t chipId = ESP.getChipId();
  char idBuf[24];
  snprintf(idBuf, sizeof(idBuf), "esp_monitor_%06X", chipId & 0xFFFFFF);
  mqttClientId = String(idBuf);

  String base = "esp_monitor/" + mqttClientId;
  topicSensor       = base + "/sensor";
  topicState        = base + "/state";
  topicSync         = base + "/sync";
  topicCmnd         = base + "/command/#";
  topicCmndPower    = base + "/command/power";
  topicCmndFanSpeed = base + "/command/fanspeed";
  topicCmndOscillate= base + "/command/oscillate";
  topicCmndTimer    = base + "/command/timer";
  topicCmndState    = base + "/command/state";

  Serial.print("[MQTT] Client ID: ");
  Serial.println(mqttClientId);
}

// ======== Publish Functions ========

void publishFanState()
{
  if (!mqttClient.connected())
    return;

  JsonDocument doc;
  doc["POWER"] = fanPower ? "ON" : "OFF";
  doc["FanSpeed"] = fanSpeed;
  doc["Oscillate"] = fanOscillate ? "ON" : "OFF";
  doc["Timer"] = fanTimer;

  char buf[128];
  serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(topicState.c_str(), buf);
  Serial.print("[MQTT] STATE: ");
  Serial.println(buf);
}

void publishSensorData()
{
  if (!mqttClient.connected())
    return;

  JsonDocument doc;

  // Time
  char timeBuf[21] = "1970-01-01T00:00:00";
  if (ntpSynced)
  {
    time_t now = time(nullptr);
    struct tm *ti = localtime(&now);
    if (ti && ti->tm_year > (2020 - 1900))
    {
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", ti);
    }
  }
  doc["Time"] = timeBuf;

  JsonObject aht20 = doc["AHT2X"].to<JsonObject>();
  aht20["Temperature"] = serialized(String(cachedTemp, 1));
  aht20["Humidity"] = serialized(String(cachedHum, 1));
  aht20["DewPoint"] = serialized(String(cachedDewPoint, 1));

  doc["TempUnit"] = "C";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  mqttClient.publish(topicSensor.c_str(), buf);
  Serial.print("[MQTT] SENSOR: ");
  Serial.println(buf);
}

void publishHADiscovery()
{
  if (!mqttClient.connected())
    return;

  // Shared device info for all sensors
  // We'll build each config individually to keep stack usage reasonable

  struct SensorConfig {
    const char *id_suffix;
    const char *name;
    const char *device_class;
    const char *unit;
    const char *value_template;
  };

  SensorConfig sensors[] = {
    {"temperature", "Temperature", "temperature", "°C", "{{ value_json.AHT2X.Temperature }}"},
    {"humidity",    "Humidity",    "humidity",    "%",  "{{ value_json.AHT2X.Humidity }}"},
    {"dew_point",   "Dew Point",  "temperature", "°C", "{{ value_json.AHT2X.DewPoint }}"}
  };

  for (int i = 0; i < 3; i++)
  {
    JsonDocument doc;

    String uniqueId = mqttClientId + "_" + sensors[i].id_suffix;

    doc["name"] = String("ESP Monitor ") + sensors[i].name;
    doc["state_topic"] = topicSensor;
    doc["value_template"] = sensors[i].value_template;
    doc["unit_of_measurement"] = sensors[i].unit;
    doc["device_class"] = sensors[i].device_class;
    doc["unique_id"] = uniqueId;

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = mqttClientId;
    device["name"] = "ESP Monitor";
    device["manufacturer"] = "Custom";
    device["model"] = "ESP8266 Monitor";

    String discoveryTopic = "homeassistant/sensor/" + mqttClientId + "/" + sensors[i].id_suffix + "/config";

    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(discoveryTopic.c_str(), (const uint8_t *)buf, len, true); // retain = true

    Serial.print("[MQTT] HA Discovery: ");
    Serial.println(discoveryTopic);
  }
}

void requestSyncState()
{
  if (!mqttClient.connected())
    return;

  mqttClient.publish(topicSync.c_str(), "{\"Request\":\"State\"}");
  Serial.println("[MQTT] Sync state requested");
}

// ======== MQTT Callback ========

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Null-terminate payload
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  String topicStr = String(topic);

  Serial.print("[MQTT] Received ");
  Serial.print(topicStr);
  Serial.print(": ");
  Serial.println(msg);

  if (topicStr == topicCmndPower)
  {
    String cmd = String(msg);
    cmd.toUpperCase();
    if (cmd == "ON")
      fanPower = true;
    else if (cmd == "OFF")
      fanPower = false;
    else if (cmd == "TOGGLE")
      fanPower = !fanPower;

    forceRedraw = true;
  }
  else if (topicStr == topicCmndFanSpeed)
  {
    int spd = atoi(msg);
    if (spd >= 1 && spd <= 3)
    {
      fanSpeed = spd;

      forceRedraw = true;
    }
  }
  else if (topicStr == topicCmndOscillate)
  {
    String cmd = String(msg);
    cmd.toUpperCase();
    if (cmd == "ON")
      fanOscillate = true;
    else if (cmd == "OFF")
      fanOscillate = false;
    else if (cmd == "TOGGLE")
      fanOscillate = !fanOscillate;

    forceRedraw = true;
  }
  else if (topicStr == topicCmndTimer)
  {
    int t = atoi(msg);
    if (t == 0 || t == 30 || t == 60 || t == 120)
    {
      fanTimer = t;

      forceRedraw = true;
    }
  }
  else if (topicStr == topicCmndState)
  {
    // Sync full state from Home Assistant
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (!err)
    {
      if (doc["POWER"].is<const char *>())
      {
        String p = doc["POWER"].as<String>();
        fanPower = (p == "ON");
      }
      if (doc["FanSpeed"].is<int>())
      {
        int spd = doc["FanSpeed"].as<int>();
        if (spd >= 1 && spd <= 3)
          fanSpeed = spd;
      }
      if (doc["Oscillate"].is<const char *>())
      {
        String o = doc["Oscillate"].as<String>();
        fanOscillate = (o == "ON");
      }
      if (doc["Timer"].is<int>())
      {
        int t = doc["Timer"].as<int>();
        if (t == 0 || t == 30 || t == 60 || t == 120)
          fanTimer = t;
      }

      forceRedraw = true;
      Serial.println("[MQTT] State synced from HA");
    }
  }
}
