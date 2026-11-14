#include "p1_packet.h"

const P1FieldInfo P1_FIELDS[P1_FIELD_COUNT] = {
    {"1-0:1.8.11", 3},  {"1-0:1.8.12", 3},  {"1-0:1.8.13", 3},
    {"1-0:2.8.11", 3},  {"1-0:2.8.12", 3},  {"1-0:2.8.13", 3},
    {"0-0:96.14.1", 0},
    {"1-0:1.7.0", 3},   {"1-0:2.7.0", 3},   {"1-0:21.7.0", 3},
    {"1-0:41.7.0", 3},  {"1-0:61.7.0", 3},
    {"1-0:32.7.0", 1},  {"1-0:52.7.0", 1},  {"1-0:72.7.0", 1},
    {"1-0:31.7.0", 2},  {"1-0:51.7.0", 2},  {"1-0:71.7.0", 2},
};

// Bytes per field in the packet, in P1Field order.
static constexpr uint8_t FIELD_BYTES[P1_FIELD_COUNT] = {
    4, 4, 4, 4, 4, 4,  // energy
    1,                 // tariff
    2, 2, 2, 2, 2,     // power
    2, 2, 2,           // voltage
    2, 2, 2,           // current
};

static constexpr size_t HEADER_SIZE = 11;

// C++11-compatible (ESP32 core 2.x)
static constexpr size_t payloadBytes(size_t i = 0) {
  return i == P1_FIELD_COUNT ? 0 : FIELD_BYTES[i] + payloadBytes(i + 1);
}
static_assert(HEADER_SIZE + payloadBytes() + 1 == P1_PACKET_SIZE, "packet layout");

uint8_t p1Crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static void put(uint8_t *&p, uint32_t value, uint8_t bytes) {
  for (uint8_t i = 0; i < bytes; i++) {
    *p++ = (uint8_t)(value >> (8 * i));
  }
}

static uint32_t get(const uint8_t *&p, uint8_t bytes) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < bytes; i++) {
    value |= (uint32_t)(*p++) << (8 * i);
  }
  return value;
}

void p1PacketEncode(const P1Packet &packet, uint8_t *out) {
  uint8_t *p = out;
  put(p, P1_PACKET_MAGIC, 1);
  put(p, P1_PACKET_VERSION, 1);
  put(p, packet.networkId, 2);
  put(p, packet.seq, 2);
  put(p, packet.reading.present & 0xFFFFFF, 3);
  put(p, packet.crcErrors, 2);
  for (uint8_t f = 0; f < P1_FIELD_COUNT; f++) {
    const uint8_t bytes = FIELD_BYTES[f];
    const uint32_t max = bytes == 4 ? 0xFFFFFFFFUL : (1UL << (8 * bytes)) - 1;
    const uint32_t v = packet.reading.has(f) ? packet.reading.value[f] : 0;
    put(p, v > max ? max : v, bytes);
  }
  *p = p1Crc8(out, P1_PACKET_SIZE - 1);
}

bool p1PacketDecode(const uint8_t *data, size_t length, P1Packet &packet) {
  if (length != P1_PACKET_SIZE || data[0] != P1_PACKET_MAGIC || data[1] != P1_PACKET_VERSION) {
    return false;
  }
  if (p1Crc8(data, P1_PACKET_SIZE - 1) != data[P1_PACKET_SIZE - 1]) {
    return false;
  }
  const uint8_t *p = data + 2;
  packet.networkId = (uint16_t)get(p, 2);
  packet.seq = (uint16_t)get(p, 2);
  packet.reading.present = get(p, 3) & ((1UL << P1_FIELD_COUNT) - 1);
  packet.crcErrors = (uint16_t)get(p, 2);
  for (uint8_t f = 0; f < P1_FIELD_COUNT; f++) {
    packet.reading.value[f] = get(p, FIELD_BYTES[f]);
  }
  return true;
}

int p1PacketFind(const uint8_t *data, size_t length, P1Packet &packet) {
  for (size_t i = 0; i + P1_PACKET_SIZE <= length; i++) {
    if (data[i] == P1_PACKET_MAGIC && p1PacketDecode(data + i, P1_PACKET_SIZE, packet)) {
      return (int)i;
    }
  }
  return -1;
}
