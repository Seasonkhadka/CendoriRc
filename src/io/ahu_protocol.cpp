#include "ahu_protocol.h"

#include <cstring>

#include "ahu_link.h"

namespace ahu_protocol {

namespace {

constexpr uint8_t kHeaderStatusCmd = 0xD5;
constexpr uint8_t kHeaderPowerCmd = 0xE5;
constexpr uint8_t kHeaderPowerResp = 0xB5;
constexpr uint8_t kHeaderCleanCmd = 0xE7;
constexpr uint8_t kHeaderCleanResp = 0xB7;
constexpr uint8_t kHeaderFanCmd = 0xC5;
constexpr uint8_t kHeaderFanResp = 0xA5;

constexpr uint8_t kGroup = 0x00;
// Real id is 0x62, not 0x64 - the echoed id field in every B5 reply so
// far just mirrored whatever id we put in the request (0x64 in, 0x64
// back; 0x62 in, 0x62 back), so it was never actual proof of this unit's
// real address. 0x62 per direct correction, pending retest.
constexpr uint8_t kId = 0x62;
constexpr uint8_t kNoChange = 0xFE;
constexpr uint8_t kRequest = 0x00;

// Same Mode5 masks as CendoriAppDC AhuStruct.hpp
constexpr uint8_t kModeCheck = 0xF8;
constexpr uint8_t kPowerCheck = 0x70;
constexpr uint8_t kVentilation = 0x10;
constexpr uint8_t kStartReserve = 0x20;
constexpr uint8_t kStopReserve = 0x30;
constexpr uint8_t kBypass = 0x80;
constexpr uint8_t kAutoDrive = 0x08;
constexpr uint8_t kLock = 0x04;
constexpr uint8_t kVolumeCheck = 0x03;
constexpr uint8_t kVolumeL = 0x01;
constexpr uint8_t kClean = 0x10;

constexpr uint32_t kMinCommandGapMs = 1500;
constexpr uint32_t kAfterStatusMs = 100;
constexpr uint32_t kAfterCleanOffMs = 300;
constexpr uint32_t kAfterPowerBeforeCleanMs = 200;
constexpr uint32_t kAfterVolumeBeforeCleanMs = 100;

uint8_t s_shadowMode = 0x00;
uint32_t s_lastCommandMs = 0;
bool s_cleanOn = false;

bool rateLimitOk() {
  uint32_t now = millis();
  if (s_lastCommandMs != 0 && now - s_lastCommandMs < kMinCommandGapMs) {
    Serial.println("[AHU] command rejected - too soon after the last one");
    return false;
  }
  return true;
}

void buildFrame(uint8_t header, const uint8_t payload[6], uint8_t *frame /* 10 bytes */) {
  frame[0] = header;
  frame[1] = kGroup;
  frame[2] = kId;
  memcpy(&frame[3], payload, 6);
  uint8_t parity = 0;
  for (int i = 0; i < 9; i++) parity ^= frame[i];
  frame[9] = parity;
}

bool responseOk(const uint8_t *resp, size_t len, uint8_t expectedHeader) {
  if (len != 10) {
    Serial.printf("[AHU] response wrong length: got %u, want 10\n", (unsigned)len);
    return false;
  }
  if (resp[0] != expectedHeader) {
    Serial.printf("[AHU] response header mismatch: got 0x%02X, want 0x%02X\n", resp[0], expectedHeader);
    return false;
  }
  if (resp[2] != kId) {
    Serial.printf("[AHU] skip other station id=0x%02X (PCB is 0x%02X)\n", resp[2], kId);
    return false;
  }
  uint8_t parity = 0;
  for (int i = 0; i < 9; i++) parity ^= resp[i];
  if (parity != resp[9]) {
    Serial.println("[AHU] response parity mismatch");
    return false;
  }
  Serial.printf("[AHU] reply id=0x%02X mode=0x%02X\n", resp[2], resp[3]);
  return true;
}

// Bus may carry other stations' traffic. Keep only a frame matching our id.
bool pickPcbFrame(const uint8_t *buf, size_t n, uint8_t expectedHeader, uint8_t *out10) {
  for (size_t i = 0; i + 10 <= n; i++) {
    if (buf[i] == expectedHeader && buf[i + 2] == kId) {
      memcpy(out10, buf + i, 10);
      return true;
    }
    if (buf[i] == expectedHeader && buf[i + 2] != kId) {
      Serial.printf("[AHU] ignored id=0x%02X frame (not PCB 0x%02X)\n", buf[i + 2], kId);
    }
  }
  return false;
}

size_t transactPcb(const uint8_t *tx, uint8_t expectedHeader, uint8_t *resp10) {
  uint8_t raw[40] = {0};
  size_t got = ahu_link::transact(tx, 10, raw, sizeof(raw));
  if (pickPcbFrame(raw, got, expectedHeader, resp10)) return 10;
  if (got >= 10) {
    memcpy(resp10, raw, 10);
    return 10;
  }
  return got;
}

// D5 status read. Updates s_shadowMode on a valid B5. Returns the live
// mode byte, or the last shadow if the unit does not answer (same idea
// as the app still sending a command after a failed status poll).
uint8_t readStatus() {
  uint8_t payload[6] = {kRequest, kRequest, kRequest, kRequest, kRequest, kRequest};
  uint8_t frame[10];
  buildFrame(kHeaderStatusCmd, payload, frame);

  uint8_t resp[10];
  size_t got = transactPcb(frame, kHeaderPowerResp, resp);
  if (!responseOk(resp, got, kHeaderPowerResp)) {
    Serial.printf("[AHU] D5 status failed - using last mode 0x%02X\n", s_shadowMode);
    return s_shadowMode;
  }

  s_shadowMode = resp[3];
  Serial.printf("[AHU] D5 status mode=0x%02X\n", s_shadowMode);
  delay(kAfterStatusMs);
  return s_shadowMode;
}

bool sendE5(uint8_t mode, uint8_t startHour, uint8_t stopHour) {
  uint8_t payload[6] = {mode, kNoChange, kNoChange, startHour, stopHour, 0x00};
  uint8_t frame[10];
  buildFrame(kHeaderPowerCmd, payload, frame);

  Serial.printf("[AHU] E5 mode=0x%02X start=0x%02X stop=0x%02X\n", mode, startHour, stopHour);

  uint8_t resp[10];
  size_t got = transactPcb(frame, kHeaderPowerResp, resp);
  if (!responseOk(resp, got, kHeaderPowerResp)) return false;

  s_shadowMode = resp[3];
  return true;
}

bool sendE7(uint8_t cleanMode) {
  uint8_t payload[6] = {kNoChange, kNoChange, kNoChange, cleanMode, kNoChange, 0x00};
  uint8_t frame[10];
  buildFrame(kHeaderCleanCmd, payload, frame);

  Serial.printf("[AHU] E7 clean=0x%02X\n", cleanMode);

  uint8_t resp[10];
  size_t got = transactPcb(frame, kHeaderCleanResp, resp);
  return responseOk(resp, got, kHeaderCleanResp);
}

uint8_t preserveReserveBits(uint8_t status) {
  return (uint8_t)(status & (kPowerCheck | kVolumeCheck | kBypass | kAutoDrive | kLock));
}

// Mode/clean packets never send volume 0 - the app defaults that to 1.
uint8_t volumeOrOne(uint8_t status) {
  uint8_t v = (uint8_t)(status & kVolumeCheck);
  return v ? v : kVolumeL;
}

// nApp: leaving clean/vent/bypass/auto sends E7 function 0x00 first.
void turnCleanOff() {
  sendE7(0x00);
  delay(kAfterCleanOffMs);
  s_cleanOn = false;
}

} // namespace

void init() {
  Serial.println("[AHU] control path: D5 then E5/E7, id=0x62 (real id, per direct correction)");
  ahu_link::init();
}

bool power_on() {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();

  // Per the protocol doc, the B5 reply to this E5 should already show the
  // new mode applied - no separate confirm poll is documented as needed.
  // Still give it a longer window than the 500ms default in case the real
  // controller is slower to ack a write than a plain status read.
  uint8_t e5[10] = {0xE5, 0x00, 0x62, 0x10, 0xFE, 0xFE, 0xFE, 0xFE, 0x00, 0x97};
  uint8_t echo[16] = {0};
  size_t n = ahu_link::transact(e5, sizeof(e5), echo, sizeof(echo), 1000);
  if (n >= 10 && echo[0] == kHeaderPowerResp && echo[2] == kId && (echo[3] & kVentilation)) {
    Serial.printf("[AHU] E5 reply mode=0x%02X - power-on applied (direct ack, per spec)\n", echo[3]);
    s_shadowMode = echo[3];
    return true;
  }
  if (n >= 4) {
    Serial.printf("[AHU] E5 echo mode=0x%02X (not applied yet - falling back to D5 re-poll)\n", echo[3]);
  }

  // Fallback only - the doc doesn't document needing this, but keep it in
  // case the real unit acks slower than expected.
  uint8_t d5[10] = {0xD5, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7};
  for (int attempt = 1; attempt <= 3; attempt++) {
    delay(300);

    uint8_t b5[16] = {0};
    size_t got = ahu_link::transact(d5, sizeof(d5), b5, sizeof(b5));
    if (got < 10 || b5[0] != kHeaderPowerResp) {
      Serial.printf("[AHU] confirm D5 attempt %d: no B5 - no reply\n", attempt);
      continue;
    }

    const uint8_t mode = b5[3];
    const bool applied = (mode & kVentilation) != 0;
    Serial.printf("[AHU] confirm D5 attempt %d mode=0x%02X -> %s\n", attempt, mode,
                  applied ? "power-on applied" : "not applied yet");
    if (applied) {
      s_shadowMode = mode;
      return true;
    }
  }

  Serial.println("[AHU] WRITE IGNORED - mode never set after 3 confirm polls");
  return false;
}

bool power_off() {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)(status & ~kPowerCheck);
  return sendE5(mode, kNoChange, kNoChange);
}

bool set_ventilation() {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  turnCleanOff();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)(kVentilation | volumeOrOne(status));
  return sendE5(mode, kNoChange, kNoChange);
}

bool set_bypass() {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  turnCleanOff();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)((status & ~kModeCheck) | kBypass | kVentilation);
  if ((mode & kVolumeCheck) == 0) mode |= kVolumeL;
  return sendE5(mode, kNoChange, kNoChange);
}

bool set_auto() {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  turnCleanOff();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)((status & ~kModeCheck) | kAutoDrive | kVentilation);
  if ((mode & kVolumeCheck) == 0) mode |= kVolumeL;
  return sendE5(mode, kNoChange, kNoChange);
}

bool set_volume(uint8_t vol) {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)((status & ~kVolumeCheck) | (vol & kVolumeCheck));
  if (!sendE5(mode, kNoChange, kNoChange)) return false;
  if (s_cleanOn) {
    delay(kAfterVolumeBeforeCleanMs);
    return sendE7(kClean);
  }
  return true;
}

bool start_reservation(uint8_t start_hour, uint8_t stop_hour) {
  (void)stop_hour; // app start packet writes start hour only; stop stays 0xFE
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)((status & ~kStopReserve) | preserveReserveBits(status) | kStartReserve);
  return sendE5(mode, start_hour, kNoChange);
}

bool stop_reservation(uint8_t stop_hour) {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();
  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)((status & ~kStopReserve) | preserveReserveBits(status) | kStopReserve);
  return sendE5(mode, kNoChange, stop_hour);
}

bool set_clean(bool on) {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();

  if (!on) {
    bool ok = sendE7(0x00);
    if (ok) s_cleanOn = false;
    return ok;
  }

  uint8_t status = readStatus();
  uint8_t mode = (uint8_t)(kVentilation | volumeOrOne(status));
  if (!sendE5(mode, kNoChange, kNoChange)) return false;
  delay(kAfterPowerBeforeCleanMs);
  bool ok = sendE7(kClean);
  if (ok) s_cleanOn = true;
  return ok;
}

bool set_fan_voltage(uint8_t fan1_low, uint8_t fan1_mid, uint8_t fan1_high,
                      uint8_t fan2_low, uint8_t fan2_mid, uint8_t fan2_high) {
  if (!rateLimitOk()) return false;
  s_lastCommandMs = millis();

  uint8_t payload[6] = {fan1_low, fan1_mid, fan1_high, fan2_low, fan2_mid, fan2_high};
  uint8_t frame[10];
  buildFrame(kHeaderFanCmd, payload, frame);

  uint8_t resp[10];
  size_t got = transactPcb(frame, kHeaderFanResp, resp);
  return responseOk(resp, got, kHeaderFanResp);
}

uint8_t shadowMode() { return s_shadowMode; }

} // namespace ahu_protocol
