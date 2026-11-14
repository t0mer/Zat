#include "lr22.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Start of the value in "+NAME=<value>" (spaces allowed around '='), or nullptr.
// Skips an echoed command such as "AT+NAME".
static const char *findAssignment(const char *text, const char *key) {
  for (const char *p = strstr(text, key); p; p = strstr(p + 1, key)) {
    const char *q = p + strlen(key);
    while (*q == ' ') q++;
    if (*q == '=') {
      q++;
      while (*q == ' ') q++;
      return q;
    }
  }
  return nullptr;
}

static const char *const AT_ENTRY_MARKERS[] = {"Entry AT", "Exit AT", nullptr};
static const char *const AT_EXIT_MARKERS[] = {"Power On", nullptr};
static const char *const AT_RESULT_MARKERS[] = {"OK", "ERROR", "EEROR", nullptr};

void LR22::logf(const char *fmt, ...) {
  char line[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  port_.log(line);
}

void LR22::drain(uint32_t quietMs) {
  uint32_t last = port_.millis();
  while (port_.millis() - last < quietMs) {
    if (port_.available()) {
      port_.read();
      last = port_.millis();
    } else {
      port_.delay(1);
    }
  }
}

bool LR22::waitIdle(uint32_t timeoutMs) {
  const uint32_t start = port_.millis();
  while (port_.auxHigh()) {
    if (port_.millis() - start >= timeoutMs) return false;
    port_.delay(1);
  }
  return true;
}

bool LR22::exchange(const char *text, char *answer, size_t answerSize, uint32_t timeoutMs,
                    const char *const *endMarkers, const char *valueKey) {
  size_t n = 0;
  answer[0] = '\0';
  port_.write((const uint8_t *)text, strlen(text));
  port_.write((const uint8_t *)"\r\n", 2);

  const uint32_t start = port_.millis();
  bool done = false;
  uint32_t doneAt = 0;
  while (port_.millis() - start < timeoutMs) {
    if (port_.available()) {
      const int c = port_.read();
      if (c > 0 && n + 1 < answerSize) {
        answer[n++] = (char)c;
        answer[n] = '\0';
      }
      if (!done) {
        for (const char *const *m = endMarkers; *m; m++) {
          if (strstr(answer, *m)) done = true;
        }
        if (valueKey) {
          const char *v = findAssignment(answer, valueKey);
          if (v && strchr(v, '\n')) done = true;
        }
        if (done) doneAt = port_.millis();
      }
    } else if (done && port_.millis() - doneAt > 30) {
      return true;  // marker seen and the line has finished
    } else {
      port_.delay(1);
    }
  }
  return done;
}

bool LR22::enterAt() {
  char answer[64];
  for (int attempt = 0; attempt < 3; attempt++) {
    waitIdle(1000);
    drain(50);
    if (!exchange("+++", answer, sizeof(answer), 1000, AT_ENTRY_MARKERS)) {
      continue;  // no answer: try again
    }
    if (strstr(answer, "Entry AT")) return true;
    // "Exit AT": it was already in AT mode and has now left (and restarts). Again.
    port_.delay(500);
    waitIdle(2000);
  }
  return false;
}

bool LR22::exitAt() {
  char answer[64];
  // "Exit AT" then the restart message "Power On"
  const bool ok = exchange("+++", answer, sizeof(answer), 3000, AT_EXIT_MARKERS);
  port_.delay(100);
  waitIdle(2000);
  drain(50);
  return ok || strstr(answer, "Exit AT") != nullptr;
}

bool LR22::configure(const LR22Setting *settings, size_t count) {
  changed_ = 0;
  if (!enterAt()) {
    logf("LR22: no answer to +++ (check wiring, 9600 baud)");
    return false;
  }
  char answer[160];
  char current[32];
  char command[48];
  for (size_t i = 0; i < count; i++) {
    const LR22Setting &s = settings[i];
    char key[24];
    snprintf(key, sizeof(key), "+%s", s.name);
    snprintf(command, sizeof(command), "AT+%s", s.name);
    if (!exchange(command, answer, sizeof(answer), 1000, AT_RESULT_MARKERS, key) ||
        !parseValue(answer, s.name, current, sizeof(current))) {
      logf("LR22: %s not supported here, skipped", s.name);
      continue;
    }
    if (sameValue(current, s.value, s.hex)) {
      logf("LR22: %s=%s ok", s.name, current);
      continue;
    }
    snprintf(command, sizeof(command), "AT+%s%s", s.name, s.value);
    if (exchange(command, answer, sizeof(answer), 1000, AT_RESULT_MARKERS) &&
        strstr(answer, "OK")) {
      logf("LR22: %s %s -> %s", s.name, current, s.value);
      changed_++;
    } else {
      logf("LR22: setting %s%s failed", s.name, s.value);
    }
  }
  if (!exitAt()) {
    logf("LR22: no answer when leaving AT mode");
  }
  return true;
}

bool LR22::send(const uint8_t *data, size_t length) {
  if (!waitIdle(500)) return false;
  port_.write(data, length);
  return true;
}

bool LR22::pollFrame(uint8_t *out, size_t outSize, size_t &length) {
  while (port_.available()) {
    const int c = port_.read();
    if (c < 0) break;
    if (rxLength_ < sizeof(rx_)) rx_[rxLength_++] = (uint8_t)c;
    lastByteMs_ = port_.millis();
  }
  if (rxLength_ == 0 || port_.millis() - lastByteMs_ < FRAME_GAP_MS) return false;
  length = rxLength_ < outSize ? rxLength_ : outSize;
  memcpy(out, rx_, length);
  rxLength_ = 0;
  return true;
}

bool LR22::parseValue(const char *text, const char *name, char *out, size_t outSize) {
  char key[24];
  snprintf(key, sizeof(key), "+%s", name);
  const char *p = findAssignment(text, key);
  if (!p) return false;
  size_t n = 0;
  while (*p && *p != '\r' && *p != '\n' && n + 1 < outSize) out[n++] = *p++;
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
  return n > 0;
}

static void normalize(const char *in, char *out, size_t outSize) {
  size_t n = 0;
  for (const char *p = in; *p && n + 1 < outSize; p++) {
    if (*p == ' ') continue;
    out[n++] = (char)tolower((unsigned char)*p);
  }
  out[n] = '\0';
}

bool LR22::sameValue(const char *a, const char *b, bool hex) {
  char na[32], nb[32];
  normalize(a, na, sizeof(na));
  normalize(b, nb, sizeof(nb));
  if (strcmp(na, nb) == 0) return true;
  // numbers: "022" == "22", "0x41" == "41" (hex)
  char *endA, *endB;
  const long va = strtol(na, &endA, hex ? 16 : 10);
  const long vb = strtol(nb, &endB, hex ? 16 : 10);
  // strtol stops at a unit suffix such as "dBm"
  return endA != na && endB != nb && va == vb;
}
