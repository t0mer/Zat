// Shared configuration: P1 meter reader over DX-LR22-900T22D (UART LoRa modem).
// Both ESP32s (transmitter at the meter, receiver near WiFi) use the same LR22 settings and
// NETWORK_ID; each LR22 is configured over AT commands at boot (see lr22.h).
#pragma once

// ---------------------------------------------------------------------------
// LR22 radio settings (applied to the module at every boot if they differ)
// ---------------------------------------------------------------------------
// Air-rate level, from the DX-LR22-900T22D application guide (5.3.2):
//   0: SF11/125k  336 bps     4: SF8/500k   10417 bps
//   1: SF11/250k 1075 bps     5: SF7/500k   18229 bps
//   2: SF11/500k 2148 bps     6: SF6/500k   37500 bps
//   3: SF8/250k  6250 bps     7: SF5/500k   62500 bps
// Level 3 sends a 59-byte reading in ~0.1 s; plenty of margin for 10-15 m.
#ifndef LR22_LEVEL
#define LR22_LEVEL          "3"
#endif
// Channel, two hex digits: 850.15 MHz + channel x 1 MHz. 41 = 915.15 MHz (module default).
// Use a frequency your local regulations allow for this kind of device.
#ifndef LR22_CHANNEL
#define LR22_CHANNEL        "41"
#endif
// Transmit power 0..22 dBm. 10 dBm is plenty for 10-15 m.
#ifndef LR22_POWER_DBM
#define LR22_POWER_DBM      "10"
#endif

// Packets with a different ID are ignored: one ID per transmitter/receiver pair.
#ifndef NETWORK_ID
#define NETWORK_ID          0xA101
#endif

// ---------------------------------------------------------------------------
// Pins (ESP32-WROOM DevKit, both sides)
// ---------------------------------------------------------------------------
#define PIN_LR22_RX         26   // ESP32 RX  <- LR22 TXD
#define PIN_LR22_TX         27   // ESP32 TX  -> LR22 RXD
#define PIN_LR22_AUX        25   // LR22 AUX: high while the module is busy
#define LR22_BAUD           9600 // module default; left unchanged

#define PIN_P1_RX           16   // transmitter only: P1 Data, same as the ESPHome project
#define P1_BAUD             115200

#define LOG_BAUD            115200

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define TX_PERIOD_MS        5000UL   // transmitter: latest valid telegram every 5 s
#define P1_STALE_MS         30000UL  // transmitter: telegrams older than this are not sent
#define STATUS_PERIOD_MS    60000UL  // transmitter: "alive, no P1 data" packet while P1 is silent
#define METER_TIMEOUT_MS    20000UL  // receiver: meter entities unavailable after this without data
#define LINK_TIMEOUT_MS     75000UL  // receiver: "LoRa link" off after this without any packet

// ---------------------------------------------------------------------------
// MQTT / Home Assistant (receiver)
// ---------------------------------------------------------------------------
#define MQTT_BASE_TOPIC     "p1lora"            // topics: p1lora/<network id>/...
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_DEVICE_NAME      "Electricity meter" // entity ids become sensor.electricity_meter_*
