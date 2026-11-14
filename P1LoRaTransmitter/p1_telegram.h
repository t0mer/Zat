// DSMR/P1 telegram parser for the IEC Kaifa three-phase meter, fed one byte at a time.
// Same rules as the ESPHome version: CRC16 (poly 0xA001, init 0) over everything from
// '/' up to and including '!', and a reading is accepted only if the CRC matches.
// Values are parsed as fixed-point decimals (no float rounding of kWh totals).
// Plain C++, no Arduino dependency, so it can be tested on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "p1_packet.h"

class P1TelegramParser {
 public:
  // Returns true when a complete telegram with a valid CRC was received;
  // the values are then in reading().
  bool feed(char c);

  const P1Reading &reading() const { return complete_; }
  uint32_t telegramsOk() const { return ok_; }
  uint32_t crcErrors() const { return crcErrors_; }

  // Parses "123.456" (stops at '*' or ')') into value * 10^decimals. Exposed for tests.
  static bool parseFixed(const char *text, uint8_t decimals, uint32_t &out);

 private:
  void parseLine();
  bool finishTelegram();

  static constexpr size_t MAX_LINE_LENGTH = 96;  // longer lines (text messages) are CRC'd, not parsed
  char line_[MAX_LINE_LENGTH + 1];
  size_t lineLength_ = 0;
  bool lineOverflow_ = false;

  bool inTelegram_ = false;
  bool inCrc_ = false;  // after '!': reading the 4 hex CRC digits
  uint16_t crc_ = 0;
  uint16_t expected_ = 0;
  uint8_t crcDigits_ = 0;

  P1Reading pending_;
  P1Reading complete_;
  uint32_t ok_ = 0;
  uint32_t crcErrors_ = 0;
};
