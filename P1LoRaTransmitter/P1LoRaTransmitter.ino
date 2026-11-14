// Transmitter: ESP32-WROOM at the meter, powered from the P1 port.
// Reads DSMR telegrams from the IEC Kaifa three-phase meter on GPIO16 (UART inverted in the
// ESP32, as in the ESPHome project, so no extra parts), checks the CRC, and sends the latest
// valid reading through the DX-LR22 every TX_PERIOD_MS. WiFi and Bluetooth stay off.
#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "lr22.h"
#include "lr22_esp32.h"
#include "p1_packet.h"
#include "p1_telegram.h"

static LR22SerialPort lr22Port(Serial1, Serial);
static LR22 lr22(lr22Port);
static const LR22Setting LR22_SETTINGS[] = {LR22_COMMON_SETTINGS, {"DRSSI", "0", false}};

static P1TelegramParser parser;
static P1Reading latest;
static uint32_t latestMs = 0;
static uint32_t latestTelegram = 0;  // parser.telegramsOk() when `latest` was taken
static uint32_t sentTelegram = 0;    // telegram number in the last data packet
static uint32_t lastTxMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastCrcErrors = 0;
static uint16_t seq = 0;

static void sendPacket(const P1Reading &reading) {
  P1Packet packet;
  packet.networkId = NETWORK_ID;
  packet.seq = ++seq;
  packet.crcErrors = (uint16_t)parser.crcErrors();
  packet.reading = reading;

  uint8_t buf[P1_PACKET_SIZE];
  p1PacketEncode(packet, buf);
  lastTxMs = millis();
  const bool ok = lr22.send(buf, sizeof(buf));
  Serial.printf("TX seq=%u %s%s\n", seq, reading.present ? "data" : "status (no P1 data)",
                ok ? "" : " FAILED: LR22 busy");
}

#if TEST_MODE
// Mock meter reading with plausible, changing values so the receiver/HA side can be verified.
static P1Reading mockReading(uint16_t n) {
  P1Reading r;
  r.present = (1UL << P1_FIELD_COUNT) - 1;
  const uint32_t power = 800 + (n % 20) * 100;  // 800..2700 W, ramps and repeats
  r.value[F_ENERGY_T1] = 12345678UL + n * 10;   // Wh
  r.value[F_ENERGY_T2] = 8765432UL;
  r.value[F_ENERGY_T3] = 0;
  r.value[F_EXPORT_T1] = 2345678UL;
  r.value[F_EXPORT_T2] = 1234567UL;
  r.value[F_EXPORT_T3] = 0;
  r.value[F_TARIFF] = 1;
  r.value[F_POWER] = power;
  r.value[F_POWER_EXPORT] = 0;
  for (int i = 0; i < 3; i++) {
    r.value[F_POWER_L1 + i] = power / 3;
    r.value[F_VOLTAGE_L1 + i] = 2300 + i * 5;  // 0.1 V -> 230.0 V..231.0 V
    r.value[F_CURRENT_L1 + i] = power / 3 * 100 / 230;  // 0.01 A
  }
  return r;
}
#endif

void setup() {
  Serial.begin(LOG_BAUD);
  WiFi.mode(WIFI_OFF);
  btStop();

  // P1: 115200 8N1, inverted; a whole telegram (~1 KB) fits in the buffer
  Serial2.setRxBufferSize(2048);
  Serial2.begin(P1_BAUD, SERIAL_8N1, PIN_P1_RX, -1, true);

  Serial.printf("\nLoRa P1 transmitter (DX-LR22), network 0x%04X\n", NETWORK_ID);
  lr22Port.begin();
  delay(500);  // module start-up
  if (lr22.configure(LR22_SETTINGS, sizeof(LR22_SETTINGS) / sizeof(LR22_SETTINGS[0]))) {
    Serial.printf("LR22 ready (%u setting(s) changed)\n", lr22.changedCount());
  } else {
    Serial.println("LR22 not answering; sending anyway with its current settings");
  }
  while (Serial1.available()) Serial1.read();
}

void loop() {
#if TEST_MODE
  while (Serial1.available()) Serial1.read();
  if (millis() - lastTxMs >= TX_PERIOD_MS || lastTxMs == 0) {
    const P1Reading mock = mockReading(seq);
    Serial.printf("TEST MODE: mock P=%luW T1=%luWh\n", (unsigned long)mock.value[F_POWER],
                  (unsigned long)mock.value[F_ENERGY_T1]);
    sendPacket(mock);
  }
  delay(1);
  return;
#endif
  while (Serial2.available()) {
    if (parser.feed((char)Serial2.read())) {
      latest = parser.reading();
      latestMs = millis();
      latestTelegram = parser.telegramsOk();
      Serial.printf("P1 ok #%lu fields=0x%05lX P=%luW T1=%luWh\n",
                    (unsigned long)latestTelegram, (unsigned long)latest.present,
                    (unsigned long)latest.value[F_POWER],
                    (unsigned long)latest.value[F_ENERGY_T1]);
    }
  }
  if (parser.crcErrors() != lastCrcErrors) {
    lastCrcErrors = parser.crcErrors();
    Serial.printf("P1 telegram dropped: bad CRC (total %lu)\n", (unsigned long)lastCrcErrors);
  }

  // Nothing is expected from the receiver; discard anything the module outputs.
  while (Serial1.available()) Serial1.read();

  const uint32_t now = millis();
  if (now - lastTxMs < TX_PERIOD_MS) {
    delay(1);
    return;
  }
  const bool fresh = latestTelegram != 0 && now - latestMs < P1_STALE_MS;
  if (fresh && latestTelegram != sentTelegram) {
    sentTelegram = latestTelegram;
    sendPacket(latest);
  } else if (!fresh && (lastStatusMs == 0 || now - lastStatusMs >= STATUS_PERIOD_MS)) {
    lastStatusMs = now;
    sendPacket(P1Reading());  // alive, but no P1 data
  }
}
