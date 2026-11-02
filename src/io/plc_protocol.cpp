#include "plc_protocol.h"

#include <stdio.h>
#include <string.h>

#include "PLCStruct.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "plc_link.h"
#include "rc/PlcService.h"
#include "rc/RcController.h"

namespace plc_protocol {

namespace {

// Per 1.3, MAIN's cycle is charge(100ms)->TX(50ms)->charge(100ms)->RX(50ms)
// -> ... - RC's reply has to land once MAIN's RX window opens, not during
// MAIN's own TX or the charge period before it. From "MAIN starts
// transmitting" to "RX window opens" is a fixed CHARGE_MS+TX_SLOT_MS=150ms,
// but we only start timing from when WE finish receiving (burst end) -
// which is already partway into that 150ms if MAIN's actual transmission
// (burst_duration_ms, measured directly, not guessed) was shorter than the
// full 50ms slot. So the remaining wait is 150 - burst_duration_ms, not a
// flat constant - this replaces the earlier delay sweep (0-300ms, which
// found no reaction at any fixed value) with a value computed per-reply
// from the real timing of the burst that triggered it.
constexpr uint32_t kReplyDelayMs = 140;

Snapshot s_snap;
Desired s_desired;
bool s_userCommand = false;
bool s_pollPending = false;
uint8_t s_pollHeader = 0;
int64_t s_lastFrameUs = 0;

void markUserCommand() { s_userCommand = true; }

void copySnapIntoDesired() {
  s_desired.mode = s_snap.mode <= 1 ? s_snap.mode : 0;
  s_desired.bypass = s_snap.bypass;
  s_desired.autoMode = s_snap.autoMode;
  s_desired.locked = s_snap.locked;
  s_desired.fanSpeed = s_snap.fanSpeed;
  s_desired.cleanMode = s_snap.cleanMode;
  s_desired.valid = true;
}

// Seeds s_desired from whatever MAIN has told us so far, the first time
// it's needed (either the UI touches a field, or a real E5 arrives) -
// never asserts zeros MAIN never actually reported. Only a one-shot: a
// request*() call after this has no reason to re-seed from a stale
// snapshot, and a later real E5 is handled separately (see
// syncDesiredFromMainIfNoOverride) so it doesn't get silently ignored.
void ensureDesiredSeeded() {
  if (s_desired.valid) return;
  copySnapIntoDesired();
}

// Unlike ensureDesiredSeeded(), this runs on every real E5 - while the UI
// hasn't asserted anything of its own yet (s_userCommand still false),
// MAIN's own live state should keep flowing into what our B5 reports,
// not freeze at whatever happened to be true the first time we replied.
// Once the user issues any request*(), s_userCommand latches true and
// MAIN's own E5 stops overwriting it - UI intent wins from then on.
void syncDesiredFromMainIfNoOverride() {
  if (s_userCommand) return;
  copySnapIntoDesired();
}

bool isKnownHeader(uint8_t h) {
  return h == PLC::D5 || h == PLC::E5 || h == PLC::D6 || h == PLC::D7 || h == PLC::E7 ||
         h == PLC::D8 || h == PLC::D9 || h == PLC::A6;
}

uint8_t checksum5(uint8_t h, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4) {
  uint8_t sum = (uint8_t)(h + d1 + d2 + d3 + d4);
  return (uint8_t)(0 - sum);
}


// All PLC frame structs (d5e5b5/d6b6/d7e7b7/d8/b8/d9/a6/c5) share the same
// header/data1/data2/data3/data4/checksum field layout - these convert
// between that typed form and the raw 6-byte wire form by field, not by
// reinterpreting memory, so there's no struct-layout/padding assumption.
template <typename T>
T frameFromBytes(const uint8_t *raw) {
  T f;
  f.header = raw[0];
  f.data1 = raw[1];
  f.data2 = raw[2];
  f.data3 = raw[3];
  f.data4 = raw[4];
  f.checksum = raw[5];
  return f;
}

template <typename T>
void frameToBytes(const T &f, uint8_t *out6) {
  out6[0] = f.header;
  out6[1] = f.data1;
  out6[2] = f.data2;
  out6[3] = f.data3;
  out6[4] = f.data4;
  out6[5] = f.checksum;
}

void logFrame(const char *dir, const uint8_t *f) {
  printf("[PLC] %s %02X %02X %02X %02X %02X %02X\n", dir, f[0], f[1], f[2], f[3], f[4], f[5]);
}

// Sliding 6-byte window on already-XOR'd bytes. Accept only an exact known
// header plus two's-complement checksum (all 6 bytes sum to 0). No 7E
// split, no header remap — those were UART-misread artifacts.

// 0xD5: outdoor temp (Data1-20), CO2 (Data2*100+Data3), error flags (Data4)
void handleD5(const PLC::d5e5b5 &f) {
  s_snap.hasD5 = true;
  s_snap.outdoorTempC = (int)f.data1 - 20;
  s_snap.outdoorCo2Ppm = (int)f.data2 * 100 + (int)f.data3;
  s_snap.d5ErrorFlags = f.data4;
}

// 0xE5: Data1 mode/fan-speed/bypass/auto/lock bits (NO_CHANGE = leave
// cached values alone); Data2 time-mode cycle, Data3 time value or (when
// Data2==TM_FUNCTION) the function-code bits.
void handleE5(const PLC::d5e5b5 &f) {
  s_snap.hasE5 = true;
  uint8_t d1 = f.data1;
  if (d1 != PLC::NO_CHANGE) {
    s_snap.bypass = (d1 & PLC::BYPASS_E5) != 0;
    s_snap.mode = (uint8_t)((d1 & PLC::MODE_MASK_E5) >> 4);
    s_snap.autoMode = (d1 & PLC::AUTO_E5) != 0;
    s_snap.locked = (d1 & PLC::LOCK_E5) != 0;
    s_snap.fanSpeed = (uint8_t)(d1 & PLC::FAN_MASK_E5);
  }
  uint8_t d2 = f.data2;
  if (d2 != PLC::NO_CHANGE) {
    s_snap.timeMode = d2;
    uint8_t d3 = f.data3;
    if (d2 == PLC::TM_FUNCTION) {
      s_snap.fireAlarm = (d3 & PLC::FIRE_E5) != 0;
      s_snap.heatEnabled = (d3 & PLC::HEAT_E5) != 0;
      s_snap.dustRemoval = (d3 & PLC::DUST_REMOVE_E5) != 0;
      s_snap.cleanMode = (d3 & PLC::CLEAN_E5) != 0;
      s_snap.heaterRunning = (d3 & PLC::HEATER_RUN_E5) != 0;
      s_snap.sleepMode = (d3 & PLC::SLEEP_E5) != 0;
      s_snap.bathInterlock = (d3 & PLC::BATH_E5) != 0;
      s_snap.hoodInterlock = (d3 & PLC::HOOD_E5) != 0;
    } else if (d3 != PLC::NO_CHANGE) {
      s_snap.timeValue = d3;
    }
  }
  syncDesiredFromMainIfNoOverride();
}

// 0xD6: heater running bit + filter wear
void handleD6(const PLC::d6b6 &f) {
  s_snap.hasD6 = true;
  s_snap.heaterRunning = (f.data1 & PLC::HEATER_RUN_D6) != 0;
  s_snap.filterRemainingHoursX100 = f.data2;
  s_snap.filterUsagePercentHi = f.data3;
  s_snap.filterUsagePercentLo = f.data4;
}

// 0xD7 (status) / 0xE7 (control): same Data3 bit layout either way.
void handleD7E7(const PLC::d7e7b7 &f) {
  s_snap.hasD7 = true;
  uint8_t d3 = f.data3;
  s_snap.heaterEnabled = (d3 & PLC::HEATER_CTRL_D7) != 0;
  s_snap.cleanMode = (d3 & PLC::CLEAN_D7) != 0;
  s_snap.exhaustMode = (d3 & PLC::EXHAUST_D7) != 0;
  s_snap.supplyMode = (d3 & PLC::SUPPLY_D7) != 0;
  s_snap.sleepMode = (d3 & PLC::SLEEP_D7) != 0;
  s_snap.d7Auto = (d3 & PLC::AUTO_D7) != 0;
}

// 0xD8: indoor temp (sign-magnitude), humidity, PM2.5, PM10. No named bit
// constants for the sign-magnitude split in the provider's pattern - kept
// as literal masks here, same as they left it.
void handleD8(const PLC::d8 &f) {
  s_snap.hasD8 = true;
  uint8_t d1 = f.data1;
  int mag = d1 & 0x7F;
  s_snap.indoorTempC = (d1 & 0x80) ? -mag : mag;
  s_snap.indoorHumidityPct = f.data2;
  s_snap.pm25 = f.data3;
  s_snap.pm10 = f.data4;
}

// 0xD9: TVOC (16-bit), temp (Data3-20), humidity (Data4)
void handleD9(const PLC::d9 &f) {
  s_snap.hasD9 = true;
  s_snap.tvoc = ((int)f.data1 << 8) | f.data2;
  s_snap.d9TempC = (int)f.data3 - 20;
  s_snap.d9HumidityPct = f.data4;
}

// 0xA6: VSP control curve - informational, this board doesn't drive fan
// motors directly.
void handleA6(const PLC::a6 &f) {
  s_snap.hasA6 = true;
  s_snap.vspMode = f.data1;
  s_snap.vspLowX2 = f.data2;
  s_snap.vspMidX2 = f.data3;
  s_snap.vspHighX2 = f.data4;
}

// The reply content comes from RcController (onPlcReplyDue), built at the
// reply moment so it carries the latest state, including an E5 just applied.
void replyTo(uint8_t header, float burstDurationMs) {
  vTaskDelay(pdMS_TO_TICKS(kReplyDelayMs));
  uint8_t out[6];
  if (!onPlcReplyDue(header, out)) return;
  bool ok = plc_link::sendBytes(out, sizeof(out));
  printf("[PLC] reply to %02X delay=%ums (burst=%.2fms)\n", header, (unsigned)kReplyDelayMs,
         burstDurationMs);
  if (ok) {
    LOGI("%02X TX: %s", out[0], PlcService::toHex(out, sizeof(out)).c_str());
  } else {
    LOGW("%02X TX FAIL: %s", out[0], PlcService::toHex(out, sizeof(out)).c_str());
  }
}

void dispatchFrame(const uint8_t *f) {
  logFrame("RX ->", f);
  switch (f[0]) {
    case PLC::D5:
      handleD5(frameFromBytes<PLC::d5e5b5>(f));
      break;
    case PLC::E5:
      handleE5(frameFromBytes<PLC::d5e5b5>(f));
      break;
    case PLC::D6:
      handleD6(frameFromBytes<PLC::d6b6>(f));
      break;
    case PLC::D7:
    case PLC::E7:
      handleD7E7(frameFromBytes<PLC::d7e7b7>(f));
      break;
    case PLC::D8:
      handleD8(frameFromBytes<PLC::d8>(f));
      break;
    case PLC::D9:
      handleD9(frameFromBytes<PLC::d9>(f));
      break;
    case PLC::A6:
      handleA6(frameFromBytes<PLC::a6>(f));
      break;
    default:
      break;
  }
  onPlcFrame(f);
}

void onPlcByte(uint8_t b) {
  static uint8_t win[6];
  static int have = 0;
  memmove(win, win + 1, 5);
  win[5] = b;
  if (have < 6) {
    have++;
    if (have < 6) return;
  }

  uint8_t sum = 0;
  for (int i = 0; i < 6; i++) sum = (uint8_t)(sum + win[i]);
  if (!isKnownHeader(win[0]) || sum != 0) return;

  printf("[PLC] FRAME OK: %02X %02X %02X %02X %02X %02X\n", win[0], win[1], win[2],
         win[3], win[4], win[5]);
  if (win[0] == PLC::D5) {
    printf("[PLC] MAIN D5 outdoor=%dC co2=%d\n", (int)win[1] - 20,
           (int)win[2] * 100 + (int)win[3]);
  } else if (win[0] == PLC::E5) {
    printf("[PLC] MAIN E5 d1=%02X (control poll)\n", win[1]);
  } else if (win[0] == PLC::D6) {
    printf("[PLC] MAIN D6 (new header after B5?)\n");
  } else if (win[0] == PLC::D7) {
    printf("[PLC] MAIN D7 (new header after B5?)\n");
  } else if (win[0] == PLC::E7) {
    printf("[PLC] MAIN E7 (new header after B5?)\n");
  }
  s_lastFrameUs = esp_timer_get_time();
  dispatchFrame(win);
  if (win[0] == PLC::D5 || win[0] == PLC::E5 || win[0] == PLC::D6 || win[0] == PLC::D7 ||
      win[0] == PLC::E7) {
    s_pollPending = true;
    s_pollHeader = win[0];
  }
  have = 0;
}

// Prints each byte with the gap (us) since the previous byte in this burst,
// so a scope capture of the same window can be lined up against it. At
// 2400 baud one byte (start+8+stop) is ~4167us - gaps that aren't roughly a
// multiple of that point at missed/spurious edges rather than real framing.
void logBurstTiming(const uint8_t *burst, const int64_t *gaps, size_t n) {
  char line[640];
  int pos = snprintf(line, sizeof(line), "[PLC] burst len=%u:", (unsigned)n);
  for (size_t i = 0; i < n && pos < (int)sizeof(line) - 24; i++) {
    pos += snprintf(line + pos, sizeof(line) - pos, " %02X(+%lldus)", burst[i],
                     (long long)gaps[i]);
  }
  printf("%s\n", line);
}

void flushBurst(uint8_t *burst, const int64_t *gaps, size_t n) {
  if (n == 0) return;
  logBurstTiming(burst, gaps, n);
}

// Diagnostic only, protocol-provider-requested: cumulative count of bytes
// that made it into a burst[] array in poll() below, printed alongside
// plc_link::uartBytesReadCount() at every burst dump. If the two cumulative
// totals track together, our own ring/burst pipeline isn't the one losing
// bytes - whatever's missing never reached the UART peripheral in the first
// place (a real line/hardware issue). If plc_link's count runs meaningfully
// ahead of this one, bytes are reaching the driver but getting lost or
// misplaced somewhere in our own ring buffer or burst grouping - our bug.
uint32_t s_burstBytesLogged = 0;

// Objective before/after metric for hardware changes (e.g. R46 150K->82K):
// what fraction of bursts actually decode to a known frame, over a rolling
// window - printed periodically so two test runs can be compared by a
// number instead of eyeballing scrollback.
void recordBurstHealth(bool decoded) {
  static int total = 0;
  static int ok = 0;
  static int64_t windowStartUs = 0;
  constexpr int64_t kWindowUs = 10 * 1000 * 1000; // 10s

  int64_t now = esp_timer_get_time();
  if (windowStartUs == 0) windowStartUs = now;

  total++;
  if (decoded) ok++;

  if (now - windowStartUs >= kWindowUs) {
    double pct = total > 0 ? (100.0 * ok / total) : 0.0;
    printf("[PLC] health: %d/%d bursts decoded (%.1f%%) last %llds\n", ok, total, pct,
           (long long)((now - windowStartUs) / 1000000));
    total = 0;
    ok = 0;
    windowStartUs = now;
  }
}

} // namespace

void init() {
  printf("[PLC] replies from RcController state, delay 140ms, TX XOR FF\n");
}

uint32_t plcMsSinceLastFrame() {
  return (uint32_t)((esp_timer_get_time() - s_lastFrameUs) / 1000);
}

void poll() {
  static uint8_t burst[128];
  static int64_t gaps[128];
  static size_t n = 0;
  static int64_t lastRealUs = 0;
  static int64_t burstStartUs = 0;
  static int64_t prevEndUs = 0;
  constexpr int64_t kGapUs = 30 * 1000;

  auto dumpRaw = [&]() {
    if (n == 0) return;
    int64_t durUs = lastRealUs - burstStartUs;
    int64_t sinceUs = (prevEndUs == 0) ? 0 : (burstStartUs - prevEndUs);
    printf("[PLC] interval=%lldms  frame=%lldms  ", (long long)(sinceUs / 1000),
           (long long)(durUs / 1000));
    flushBurst(burst, gaps, n);
    float burstDurationMs = (float)durUs / 1000.0f;
    bool gotPoll = s_pollPending;
    if (s_pollPending) {
      replyTo(s_pollHeader, burstDurationMs);
      s_pollPending = false;
    }
    recordBurstHealth(gotPoll);
    printf("[DIAG] uart_fifo_bytes=%u burst_logged=%u (cumulative, diff=%d)\n",
           (unsigned)plc_link::uartBytesReadCount(), (unsigned)s_burstBytesLogged,
           (int)(plc_link::uartBytesReadCount() - s_burstBytesLogged));
    prevEndUs = lastRealUs;
    n = 0;
  };

  uint8_t b;
  int64_t ts;
  while (plc_link::readTimedByte(&b, &ts)) {
    if (n > 0 && ts - lastRealUs > kGapUs) dumpRaw();
    if (n == 0) burstStartUs = ts;
    gaps[n] = (n == 0) ? 0 : (ts - lastRealUs);
    lastRealUs = ts;
    if (n >= sizeof(burst)) dumpRaw();
    burst[n++] = b;
    s_burstBytesLogged++;
    onPlcByte(b);
  }

  if (n > 0 && esp_timer_get_time() - lastRealUs > kGapUs) dumpRaw();
}

const Snapshot &snapshot() { return s_snap; }

void requestMode(uint8_t mode) {
  printf("[PLC] requestMode %u\n", (unsigned)mode);
  ensureDesiredSeeded();
  s_desired.mode = mode;
  markUserCommand();
}

void requestBypass(bool on) {
  ensureDesiredSeeded();
  s_desired.bypass = on;
  markUserCommand();
}

void requestAutoMode(bool on) {
  ensureDesiredSeeded();
  s_desired.autoMode = on;
  markUserCommand();
}

void requestLocked(bool on) {
  ensureDesiredSeeded();
  s_desired.locked = on;
  markUserCommand();
}

void requestFanSpeed(uint8_t speed) {
  ensureDesiredSeeded();
  s_desired.fanSpeed = (uint8_t)(speed & 0x03);
  markUserCommand();
}

void requestCleanMode(bool on) {
  ensureDesiredSeeded();
  s_desired.cleanMode = on;
  markUserCommand();
}

// Diagnostic only - see header comment. Deliberately does NOT call
// replyB5(), does NOT touch s_desired/s_snap, and is never invoked from
// poll() - the only caller is a one-shot trigger in main.cpp. The static
// guard enforces "exactly once" even on an accidental second call, since a
// surprise second TX would contaminate the one-send isolation this exists
// to provide.
void sendDiagnosticProbeOnce() {
  static bool s_fired = false;
  if (s_fired) {
    printf("[PLC] DIAG probe already sent once - ignoring repeat call\n");
    return;
  }
  s_fired = true;

  PLC::d5e5b5 probe;
  probe.header = PLC::B5;
  probe.checksum = checksum5(probe.header, probe.data1, probe.data2, probe.data3, probe.data4);
  uint8_t frame[6];
  frameToBytes(probe, frame);

  int64_t t0 = esp_timer_get_time();
  bool ok = plc_link::sendBytes(frame, sizeof(frame));
  printf("[PLC] DIAG probe t=%lldus %s %02X %02X %02X %02X %02X %02X\n",
         (long long)t0, ok ? "TX ->" : "TX FAIL", frame[0], frame[1], frame[2],
         frame[3], frame[4], frame[5]);
}

const Desired &desired() { return s_desired; }

} // namespace plc_protocol
