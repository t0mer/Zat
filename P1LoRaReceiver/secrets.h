// WiFi, MQTT and OTA settings for the receiver: fill these in before uploading.
#pragma once

#define WIFI_SSID      ""       // 2.4 GHz
#define WIFI_PASSWORD  ""

#define MQTT_HOST      ""    // e.g. Home Assistant with the Mosquitto add-on
#define MQTT_PORT      1883
#define MQTT_USER      ""       // nullptr if the broker allows anonymous clients
#define MQTT_PASS      ""

#define OTA_HOSTNAME   ""
#define OTA_PASSWORD   ""      // "" = no password
