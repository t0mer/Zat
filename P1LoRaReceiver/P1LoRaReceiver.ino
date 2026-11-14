// Receiver: ESP32-WROOM + DX-LR22. Receives meter readings over LoRa and publishes them to
// MQTT with Home Assistant discovery. The entities keep the names of the ESPHome project
// (device "Electricity meter": sensor.electricity_meter_power, ..._energy_total, ...), so its
// homeassistant/configuration.yaml and dashboard.yaml work unchanged.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "config.h"
#include "lr22.h"
#include "lr22_esp32.h"
#include "p1_packet.h"
#include "secrets.h"

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
struct Entity {
  const char *key;         // topic suffix and unique_id suffix
  const char *name;        // HA entity name (same as the ESPHome project)
  int8_t field;            // P1Field, or -1 for computed / diagnostic values
  const char *unit;
  const char *deviceClass;
  const char *stateClass;
  uint8_t displayDecimals;
  bool diagnostic;         // diagnostic: stays available when the meter data is missing
};

static const Entity ENTITIES[] = {
    {"energy_t1", "Energy T1", F_ENERGY_T1, "kWh", "energy", "total_increasing", 3, false},
    {"energy_t2", "Energy T2", F_ENERGY_T2, "kWh", "energy", "total_increasing", 3, false},
    {"energy_t3", "Energy T3", F_ENERGY_T3, "kWh", "energy", "total_increasing", 3, false},
    {"export_t1", "Export T1", F_EXPORT_T1, "kWh", "energy", "total_increasing", 3, false},
    {"export_t2", "Export T2", F_EXPORT_T2, "kWh", "energy", "total_increasing", 3, false},
    {"export_t3", "Export T3", F_EXPORT_T3, "kWh", "energy", "total_increasing", 3, false},
    {"energy_total", "Energy Total", -1, "kWh", "energy", "total_increasing", 3, false},
    {"export_total", "Export Total", -1, "kWh", "energy", "total_increasing", 3, false},
    {"power", "Power", F_POWER, "kW", "power", "measurement", 3, false},
    {"power_export", "Power Export", F_POWER_EXPORT, "kW", "power", "measurement", 3, false},
    {"power_l1", "Power L1", F_POWER_L1, "kW", "power", "measurement", 3, false},
    {"power_l2", "Power L2", F_POWER_L2, "kW", "power", "measurement", 3, false},
    {"power_l3", "Power L3", F_POWER_L3, "kW", "power", "measurement", 3, false},
    {"voltage_l1", "Voltage L1", F_VOLTAGE_L1, "V", "voltage", "measurement", 1, false},
    {"voltage_l2", "Voltage L2", F_VOLTAGE_L2, "V", "voltage", "measurement", 1, false},
    {"voltage_l3", "Voltage L3", F_VOLTAGE_L3, "V", "voltage", "measurement", 1, false},
    {"current_l1", "Current L1", F_CURRENT_L1, "A", "current", "measurement", 1, false},
    {"current_l2", "Current L2", F_CURRENT_L2, "A", "current", "measurement", 1, false},
    {"current_l3", "Current L3", F_CURRENT_L3, "A", "current", "measurement", 1, false},
    {"tariff", "Active Tariff", F_TARIFF, nullptr, nullptr, nullptr, 0, false},
    {"rssi", "LoRa RSSI", -1, "dBm", "signal_strength", "measurement", 0, true},
    {"p1_crc_errors", "P1 CRC errors", -1, nullptr, nullptr, "total_increasing", 0, true},
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static char base[32];       // p1lora/a101
static char nodeId[24];     // p1lora_a101

static LR22SerialPort lr22Port(Serial1, Serial);
static LR22 lr22(lr22Port);
// DRSSI on: the module appends one RSSI byte to every received packet
static const LR22Setting LR22_SETTINGS[] = {LR22_COMMON_SETTINGS, {"DRSSI", "1", false}};
static uint32_t lastPacketMs = 0;  // any valid packet
static uint32_t lastDataMs = 0;    // packet with meter data
static bool meterOnline = false;
static bool linkOn = false;
static bool discoveryDue = true;
static uint32_t lastMqttAttemptMs = 0;
static uint32_t packets = 0;
static uint32_t lostPackets = 0;
static int32_t lastSeq = -1;

// ---------------------------------------------------------------------------
// MQTT helpers
// ---------------------------------------------------------------------------
static void topic(char *out, size_t size, const char *suffix) {
  snprintf(out, size, "%s/%s", base, suffix);
}

static void publish(const char *suffix, const char *payload) {
  char t[64];
  topic(t, sizeof(t), suffix);
  mqtt.publish(t, payload, true);  // retained: HA gets the last values after a restart
}

// value / 10^decimals as text, without float rounding ("12345.678")
static void formatFixed(char *out, size_t size, uint32_t value, uint8_t decimals) {
  char digits[16];
  int n = snprintf(digits, sizeof(digits), "%lu", (unsigned long)value);
  // left-pad with zeros so there is at least one digit before the point
  char padded[24];
  int pad = decimals + 1 - n;
  if (pad < 0) pad = 0;
  memset(padded, '0', pad);
  memcpy(padded + pad, digits, n + 1);
  n += pad;
  if (decimals == 0) {
    snprintf(out, size, "%s", padded);
  } else {
    snprintf(out, size, "%.*s.%s", n - decimals, padded, padded + n - decimals);
  }
}

static void publishDiscovery() {
  char availBridge[64], availMeter[64];
  topic(availBridge, sizeof(availBridge), "bridge");
  topic(availMeter, sizeof(availMeter), "meter");

  static char payload[900];
  char cfgTopic[128], stateTopic[64];
  const char *device =
      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"" HA_DEVICE_NAME
      "\",\"mf\":\"DIY\",\"mdl\":\"LoRa P1 reader (Kaifa 3-phase)\"}";
  char deviceJson[160];
  snprintf(deviceJson, sizeof(deviceJson), device, nodeId);

  for (const Entity &e : ENTITIES) {
    topic(stateTopic, sizeof(stateTopic), e.key);
    snprintf(cfgTopic, sizeof(cfgTopic), HA_DISCOVERY_PREFIX "/sensor/%s/%s/config", nodeId,
             e.key);
    int n = snprintf(payload, sizeof(payload),
                     "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s\"", e.name, nodeId,
                     e.key, stateTopic);
    if (e.unit) n += snprintf(payload + n, sizeof(payload) - n, ",\"unit_of_meas\":\"%s\"", e.unit);
    if (e.deviceClass)
      n += snprintf(payload + n, sizeof(payload) - n, ",\"dev_cla\":\"%s\"", e.deviceClass);
    if (e.stateClass)
      n += snprintf(payload + n, sizeof(payload) - n, ",\"stat_cla\":\"%s\"", e.stateClass);
    if (e.field == F_TARIFF) n += snprintf(payload + n, sizeof(payload) - n, ",\"ic\":\"mdi:cash-clock\"");
    n += snprintf(payload + n, sizeof(payload) - n, ",\"sug_dsp_prc\":%u", e.displayDecimals);
    if (e.diagnostic) {
      n += snprintf(payload + n, sizeof(payload) - n,
                    ",\"ent_cat\":\"diagnostic\",\"avty_t\":\"%s\"", availBridge);
    } else {
      n += snprintf(payload + n, sizeof(payload) - n,
                    ",\"avty\":[{\"t\":\"%s\"},{\"t\":\"%s\"}],\"avty_mode\":\"all\"", availBridge,
                    availMeter);
    }
    snprintf(payload + n, sizeof(payload) - n, ",%s}", deviceJson);
    mqtt.publish(cfgTopic, payload, true);
  }

  // LoRa link: on while packets (data or status) arrive from the transmitter
  topic(stateTopic, sizeof(stateTopic), "link");
  snprintf(cfgTopic, sizeof(cfgTopic), HA_DISCOVERY_PREFIX "/binary_sensor/%s/link/config", nodeId);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"LoRa link\",\"uniq_id\":\"%s_link\",\"stat_t\":\"%s\",\"dev_cla\":"
           "\"connectivity\",\"ent_cat\":\"diagnostic\",\"avty_t\":\"%s\",%s}",
           nodeId, stateTopic, availBridge, deviceJson);
  mqtt.publish(cfgTopic, payload, true);

  publish("meter", meterOnline ? "online" : "offline");
  publish("link", linkOn ? "ON" : "OFF");
  Serial.println(F("MQTT discovery published"));
}

static void onMqttMessage(char *t, byte *payload, unsigned int length) {
  // Home Assistant restarted: send discovery again
  if (strcmp(t, HA_DISCOVERY_PREFIX "/status") == 0 && length == 6 &&
      memcmp(payload, "online", 6) == 0) {
    discoveryDue = true;
  }
}

static void serviceNetwork() {
  if (WiFi.status() != WL_CONNECTED) return;  // WiFi reconnects by itself
  if (!mqtt.connected()) {
    if (millis() - lastMqttAttemptMs < 5000 && lastMqttAttemptMs != 0) return;
    lastMqttAttemptMs = millis();
    char will[64];
    topic(will, sizeof(will), "bridge");
    Serial.print(F("MQTT connecting... "));
    if (mqtt.connect(nodeId, MQTT_USER, MQTT_PASS, will, 0, true, "offline")) {
      Serial.println(F("ok"));
      publish("bridge", "online");
      mqtt.subscribe(HA_DISCOVERY_PREFIX "/status");
      discoveryDue = true;
    } else {
      Serial.print(F("failed, state "));
      Serial.println(mqtt.state());
      return;
    }
  }
  mqtt.loop();
  if (discoveryDue) {
    discoveryDue = false;
    publishDiscovery();
  }
}

// ---------------------------------------------------------------------------
// Packets
// ---------------------------------------------------------------------------
static void publishReading(const P1Packet &packet) {
  char value[24];
  const P1Reading &r = packet.reading;

  for (const Entity &e : ENTITIES) {
    if (e.field < 0 || !r.has(e.field)) continue;
    formatFixed(value, sizeof(value), r.value[e.field], P1_FIELDS[e.field].decimals);
    publish(e.key, value);
  }
  if (r.has(F_ENERGY_T1) && r.has(F_ENERGY_T2) && r.has(F_ENERGY_T3)) {
    formatFixed(value, sizeof(value),
                r.value[F_ENERGY_T1] + r.value[F_ENERGY_T2] + r.value[F_ENERGY_T3], 3);
    publish("energy_total", value);
  }
  if (r.has(F_EXPORT_T1) && r.has(F_EXPORT_T2) && r.has(F_EXPORT_T3)) {
    formatFixed(value, sizeof(value),
                r.value[F_EXPORT_T1] + r.value[F_EXPORT_T2] + r.value[F_EXPORT_T3], 3);
    publish("export_total", value);
  }
}

static void handlePacket(const P1Packet &packet, int rssi) {
  if (packet.networkId != NETWORK_ID) {
    Serial.printf("RX from another network (0x%04X), ignored\n", packet.networkId);
    return;
  }

  packets++;
  if (lastSeq >= 0) {
    const uint16_t gap = (uint16_t)(packet.seq - (uint16_t)lastSeq);
    if (gap > 1 && gap < 1000) lostPackets += gap - 1;  // larger jump = transmitter restarted
  }
  lastSeq = packet.seq;
  lastPacketMs = millis();

  Serial.printf("RX seq=%u fields=0x%05lX RSSI=%d received=%lu lost=%lu\n", packet.seq,
                (unsigned long)packet.reading.present, rssi, (unsigned long)packets,
                (unsigned long)lostPackets);
  if (!mqtt.connected()) return;

  char value[16];
  if (rssi != 0) {
    snprintf(value, sizeof(value), "%d", rssi);
    publish("rssi", value);
  }
  snprintf(value, sizeof(value), "%u", packet.crcErrors);
  publish("p1_crc_errors", value);
  if (!linkOn) {
    linkOn = true;
    publish("link", "ON");
  }

  if (packet.reading.present == 0) return;  // status packet: transmitter alive, no P1 data
  lastDataMs = lastPacketMs;
  publishReading(packet);
  if (!meterOnline) {
    meterOnline = true;
    publish("meter", "online");
  }
}

// One burst from the LR22 normally holds one packet + its RSSI byte; after a long MQTT
// reconnect it can hold several.
static void serviceRadio() {
  uint8_t frame[256];
  size_t length;
  if (!lr22.pollFrame(frame, sizeof(frame), length)) return;
  size_t offset = 0;
  bool any = false;
  while (offset + P1_PACKET_SIZE <= length) {
    P1Packet packet;
    const int found = p1PacketFind(frame + offset, length - offset, packet);
    if (found < 0) break;
    const size_t end = offset + (size_t)found + P1_PACKET_SIZE;
    // RSSI byte right after the packet: dBm = -(255 - byte)
    const int rssi = end < length ? -(255 - (int)frame[end]) : 0;
    handlePacket(packet, rssi);
    any = true;
    offset = end + (end < length ? 1 : 0);
  }
  if (!any) {
    Serial.printf("RX %u bytes, no valid packet\n", (unsigned)length);
  }
}

static void checkTimeouts() {
  const uint32_t now = millis();
  if (meterOnline && now - lastDataMs > METER_TIMEOUT_MS) {
    meterOnline = false;
    Serial.println(F("No meter data: entities unavailable"));
    if (mqtt.connected()) publish("meter", "offline");
  }
  if (linkOn && now - lastPacketMs > LINK_TIMEOUT_MS) {
    linkOn = false;
    Serial.println(F("LoRa link lost"));
    if (mqtt.connected()) publish("link", "OFF");
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(LOG_BAUD);
  snprintf(base, sizeof(base), MQTT_BASE_TOPIC "/%04x", NETWORK_ID);
  snprintf(nodeId, sizeof(nodeId), MQTT_BASE_TOPIC "_%04x", NETWORK_ID);
  Serial.printf("\nLoRa P1 receiver (DX-LR22), network 0x%04X, topics %s/...\n", NETWORK_ID,
                base);

  Serial1.setRxBufferSize(1024);
  lr22Port.begin();
  delay(500);  // module start-up
  if (lr22.configure(LR22_SETTINGS, sizeof(LR22_SETTINGS) / sizeof(LR22_SETTINGS[0]))) {
    Serial.printf("LR22 ready (%u setting(s) changed)\n", lr22.changedCount());
  } else {
    Serial.println("LR22 not answering; listening anyway with its current settings");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);  // discovery payloads are ~600 bytes
  mqtt.setCallback(onMqttMessage);

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
}

void loop() {
  static bool wasConnected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      Serial.print(F("WiFi connected, IP "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("WiFi disconnected"));
    }
  }

  ArduinoOTA.handle();
  serviceRadio();
  serviceNetwork();
  checkTimeouts();
  delay(2);
}
