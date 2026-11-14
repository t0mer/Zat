#include "p1_telegram.h"

#include <string.h>

static uint16_t crcUpdate(uint16_t crc, uint8_t byte) {
  crc ^= byte;
  for (int bit = 0; bit < 8; bit++) {
    crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool P1TelegramParser::parseFixed(const char *text, uint8_t decimals, uint32_t &out) {
  uint64_t value = 0;
  bool anyDigit = false;
  bool afterPoint = false;
  uint8_t fraction = 0;
  bool roundUp = false;
  for (const char *p = text; *p && *p != '*' && *p != ')'; p++) {
    if (*p == '.') {
      if (afterPoint) return false;
      afterPoint = true;
    } else if (*p >= '0' && *p <= '9') {
      anyDigit = true;
      if (afterPoint && fraction >= decimals) {
        // Extra decimals: round on the first dropped digit only.
        if (fraction == decimals) roundUp = *p >= '5';
        fraction = (uint8_t)(decimals + 1);
        continue;
      }
      value = value * 10 + (uint64_t)(*p - '0');
      if (afterPoint) fraction++;
      if (value > 0xFFFFFFFFULL * 10) return false;
    } else {
      return false;
    }
  }
  if (!anyDigit) return false;
  if (fraction > decimals) fraction = decimals;  // the rounded-off case
  for (; fraction < decimals; fraction++) value *= 10;
  if (roundUp) value++;
  if (value > 0xFFFFFFFFULL) return false;
  out = (uint32_t)value;
  return true;
}

void P1TelegramParser::parseLine() {
  if (lineOverflow_ || lineLength_ == 0) return;
  line_[lineLength_] = '\0';
  char *paren = strchr(line_, '(');
  if (paren == nullptr) return;
  *paren = '\0';
  for (uint8_t f = 0; f < P1_FIELD_COUNT; f++) {
    if (strcmp(line_, P1_FIELDS[f].obis) == 0) {
      uint32_t v;
      if (parseFixed(paren + 1, P1_FIELDS[f].decimals, v)) {
        pending_.value[f] = v;
        pending_.present |= 1UL << f;
      }
      return;
    }
  }
}

bool P1TelegramParser::finishTelegram() {
  inTelegram_ = false;
  inCrc_ = false;
  if (crcDigits_ != 4 || expected_ != crc_) {
    crcErrors_++;
    return false;
  }
  complete_ = pending_;
  ok_++;
  return true;
}

bool P1TelegramParser::feed(char c) {
  // '/' begins a telegram, even in the middle of a broken one (bytes lost on the line).
  // DSMR data lines never contain '/'.
  if (c == '/') {
    if (inTelegram_) crcErrors_++;  // previous telegram never finished
    inTelegram_ = true;
    inCrc_ = false;
    crc_ = 0;
    pending_ = P1Reading();
    lineLength_ = 0;
    lineOverflow_ = false;
  }
  if (!inTelegram_) return false;

  if (inCrc_) {
    const int h = hexValue(c);
    if (h >= 0 && crcDigits_ < 4) {
      expected_ = (uint16_t)((expected_ << 4) | h);
      crcDigits_++;
      return false;
    }
    if (c == '\r' || c == '\n') {
      return finishTelegram();
    }
    crcDigits_ = 0xFF;  // garbage after '!': fails on the line end
    return false;
  }

  crc_ = crcUpdate(crc_, (uint8_t)c);
  if (c == '!') {
    inCrc_ = true;
    expected_ = 0;
    crcDigits_ = 0;
    return false;
  }
  if (c == '\n') {
    parseLine();
    lineLength_ = 0;
    lineOverflow_ = false;
  } else if (c != '\r') {
    if (lineLength_ < MAX_LINE_LENGTH) {
      line_[lineLength_++] = c;
    } else {
      lineOverflow_ = true;
    }
  }
  return false;
}
