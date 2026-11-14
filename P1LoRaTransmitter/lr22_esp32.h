// LR22Port on an ESP32 HardwareSerial + AUX pin, and the settings both sides apply.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "lr22.h"

class LR22SerialPort : public LR22Port {
 public:
  LR22SerialPort(HardwareSerial &serial, Print &logOut) : serial_(serial), log_(logOut) {}

  void begin() {
    pinMode(PIN_LR22_AUX, INPUT);
    serial_.begin(LR22_BAUD, SERIAL_8N1, PIN_LR22_RX, PIN_LR22_TX);
  }

  int available() override { return serial_.available(); }
  int read() override { return serial_.read(); }
  size_t write(const uint8_t *data, size_t length) override {
    const size_t n = serial_.write(data, length);
    serial_.flush();  // wait until the bytes are out, so AUX reflects them
    return n;
  }
  bool auxHigh() override { return digitalRead(PIN_LR22_AUX) == HIGH; }
  uint32_t millis() override { return ::millis(); }
  void delay(uint32_t ms) override { ::delay(ms); }
  void log(const char *line) override { log_.println(line); }

 private:
  HardwareSerial &serial_;
  Print &log_;
};

// Transparent mode, always receiving ("high aging" mode), M0/M1 ignored, 230-byte
// sub-packets (one 59-byte reading = one LoRa packet), CRC on, IQ inversion on (default).
// DRSSI differs per side: the receiver wants the RSSI byte after each packet.
#define LR22_COMMON_SETTINGS                 \
  {"MODE", "0", false},                      \
  {"SLEEP", "2", false},                     \
  {"SWITCH", "0", false},                    \
  {"LEVEL", LR22_LEVEL, false},              \
  {"CHANNEL", LR22_CHANNEL, true},           \
  {"POWE", LR22_POWER_DBM, false},           \
  {"PACKET", "3", false},                    \
  {"CRC", "1", false},                       \
  {"IQ", "1", false}
