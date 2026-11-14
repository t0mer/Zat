// Over-the-air format of a meter reading. Values are fixed-point integers, so no precision
// is lost (kWh with 3 decimals as Wh, kW as W, V in 0.1 V, A in 0.01 A).
//
// Layout (little-endian), P1_PACKET_SIZE = 59 bytes:
//   0     magic 0xE1
//   1     version
//   2-3   network ID
//   4-5   sequence number
//   6-8   present mask (bit i = field i was in the telegram); 0 = status packet, no P1 data
//   9-10  P1 telegrams dropped for bad CRC since the transmitter started (wraps)
//   11-34 energy T1..T3 imported, T1..T3 exported, 6 x uint32 Wh
//   35    active tariff
//   36-45 power import, export, L1, L2, L3, 5 x uint16 W
//   46-51 voltage L1..L3, 3 x uint16 0.1 V
//   52-57 current L1..L3, 3 x uint16 0.01 A
//   58    CRC-8 (poly 0x07) over bytes 0-57
#pragma once

#include <stddef.h>
#include <stdint.h>

enum P1Field : uint8_t {
  F_ENERGY_T1, F_ENERGY_T2, F_ENERGY_T3,
  F_EXPORT_T1, F_EXPORT_T2, F_EXPORT_T3,
  F_TARIFF,
  F_POWER, F_POWER_EXPORT, F_POWER_L1, F_POWER_L2, F_POWER_L3,
  F_VOLTAGE_L1, F_VOLTAGE_L2, F_VOLTAGE_L3,
  F_CURRENT_L1, F_CURRENT_L2, F_CURRENT_L3,
  P1_FIELD_COUNT
};

// OBIS code, and how many decimals the stored integer keeps (kWh -> Wh = 3, kW -> W = 3,
// V -> 0.1 V = 1, A -> 0.01 A = 2).
struct P1FieldInfo {
  const char *obis;
  uint8_t decimals;
};
extern const P1FieldInfo P1_FIELDS[P1_FIELD_COUNT];

struct P1Reading {
  uint32_t present = 0;                 // bit per P1Field
  uint32_t value[P1_FIELD_COUNT] = {};  // scaled by 10^decimals
  bool has(uint8_t field) const { return present & (1UL << field); }
};

static constexpr uint8_t P1_PACKET_MAGIC = 0xE1;
static constexpr uint8_t P1_PACKET_VERSION = 1;
static constexpr size_t P1_PACKET_SIZE = 59;

struct P1Packet {
  uint16_t networkId;
  uint16_t seq;
  uint16_t crcErrors;
  P1Reading reading;
};

// Writes P1_PACKET_SIZE bytes. Values too large for their field are clamped.
void p1PacketEncode(const P1Packet &packet, uint8_t *out);

// Returns false for a wrong size, magic, version or CRC. Does not check the network ID.
bool p1PacketDecode(const uint8_t *data, size_t length, P1Packet &packet);

// Finds the first valid packet in a received byte burst (the LR22 may add bytes, e.g. the
// RSSI byte after the packet). Returns its offset, or -1.
int p1PacketFind(const uint8_t *data, size_t length, P1Packet &packet);

uint8_t p1Crc8(const uint8_t *data, size_t length);
