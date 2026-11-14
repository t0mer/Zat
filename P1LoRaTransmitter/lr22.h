// Driver for the DX-SMART DX-LR22 (LLCC68 UART LoRa modem), based on the
// "DX-LR22-900T22D serial port application guide" v2.2:
//   - after power-on the module is in transmission mode; "+++" toggles AT command mode
//     ("Entry AT" / "Exit AT"), and leaving AT mode restarts the module so new settings apply
//   - AT commands end with CR LF; queries answer "+NAME=<value>", settings answer
//     "+NAME=<value>" then "OK", errors "EEROR=<code>" (sic) or "ERROR"
//   - AUX is high while the module sends, receives or switches mode
//   - in transparent mode (AT+MODE0) bytes written to RXD are sent as-is, and received
//     packets come out of TXD; with AT+DRSSI1 one RSSI byte follows each packet
//     (dBm = -(255 - byte))
// With AT+SWITCH0 (default, kept) M0/M1 are module outputs: leave them unconnected.
//
// The driver talks to the module through LR22Port so it can be tested on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

class LR22Port {
 public:
  virtual ~LR22Port() {}
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t *data, size_t length) = 0;
  virtual bool auxHigh() = 0;
  virtual uint32_t millis() = 0;
  virtual void delay(uint32_t ms) = 0;
  virtual void log(const char *line) = 0;  // one line, no newline
};

struct LR22Setting {
  const char *name;   // AT name without "AT+", e.g. "LEVEL"
  const char *value;  // desired value as written after the name, e.g. "3"
  bool hex;           // compare as hex number (CHANNEL)
};

class LR22 {
 public:
  explicit LR22(LR22Port &port) : port_(port) {}

  // Enters AT mode, queries each setting and changes the ones that differ, then leaves AT
  // mode (which restarts the module). Settings the firmware doesn't know (query error) are
  // skipped. Returns false if the module never answered.
  bool configure(const LR22Setting *settings, size_t count);
  uint8_t changedCount() const { return changed_; }

  bool busy() { return port_.auxHigh(); }
  bool waitIdle(uint32_t timeoutMs);

  // Sends one packet in transparent mode. Returns false if the module stayed busy.
  bool send(const uint8_t *data, size_t length);

  // Collects received bytes; returns true once a burst has ended (no byte for
  // FRAME_GAP_MS) and copies it to out. Call often.
  bool pollFrame(uint8_t *out, size_t outSize, size_t &length);

  // --- helpers, public for tests -------------------------------------------------------
  // Value from a "+NAME=<value>" line in text, trimmed. False if not found.
  static bool parseValue(const char *text, const char *name, char *out, size_t outSize);
  // Equal after trimming, lower-casing and dropping leading zeros / "0x"
  // (and, for hex, comparing the numbers).
  static bool sameValue(const char *a, const char *b, bool hex);

  static constexpr uint32_t FRAME_GAP_MS = 20;  // ~20 byte times at 9600 baud

 private:
  bool enterAt();
  bool exitAt();
  // Sends text + CRLF, collects the answer until one of the end markers or the timeout.
  // A query ("AT+LEVEL") answers only "+LEVEL=<value>": pass valueKey "+LEVEL" so the
  // exchange ends at the end of that line.
  bool exchange(const char *text, char *answer, size_t answerSize, uint32_t timeoutMs,
                const char *const *endMarkers, const char *valueKey = nullptr);
  void drain(uint32_t quietMs);
  void logf(const char *fmt, ...);

  LR22Port &port_;
  uint8_t changed_ = 0;

  uint8_t rx_[256];
  size_t rxLength_ = 0;
  uint32_t lastByteMs_ = 0;
};
