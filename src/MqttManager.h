#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>

// MQTT topic strings (defined in MqttManager.cpp)
extern String mqttClientId;
extern String topicSensor;
extern String topicState;
extern String topicSync;
extern String topicCmnd;

void initMqttTopics();
void publishFanState();
void publishSensorData();
void publishHADiscovery();
void requestSyncState();
void mqttCallback(char *topic, byte *payload, unsigned int length);

#endif // MQTT_MANAGER_H
