/*
 * PLC "internal protocol" (메인 ↔ R/C), per the confidential doc
 * 대외비_ERV 내부 프로토콜_V3.2(co추가)_261012.pdf, read directly (pymupdf
 * text + rendered-page verification, not guessed/recalled).
 *
 * Fixed 6-byte frames: [Header][Data1][Data2][Data3][Data4][Checksum]
 * Checksum = two's complement of (Header+Data1+Data2+Data3+Data4), i.e.
 * every byte including Checksum sums to 0x00 mod 256. This is NOT the
 * external/RS-485 protocol's XOR checksum, and there is no group/id byte
 * here at all.
 *
 * MainControl is master: it polls, we (Room Controller) are slave. Per
 * section 1.3, MAIN's own cycle around a poll is charge/TX/charge/RX/
 * charge/TX - it transmits a D5 or E5 request, then listens briefly for
 * our B5 reply, then continues. We only ever reply to D5/E5 (both
 * answered with the same 0xB5 header, per 2.8 "RC -> MAIN (상태요청1,
 * 제어요청 응답)"). D6/D7/E7/D8/D9/A6 are MAIN pushing data to us with no
 * reply expected from our RC model (0xB6/0xB7/0xB8 responses are
 * explicitly documented as "SHR-L1(SHR-L2) 사용시 적용" only - a different
 * room-controller variant, not this board).
 *
 * MAIN never asks us to decide the ventilation mode via this channel -
 * mode/fan-speed/bypass/auto/lock arrive FROM MAIN (via E5/D7/E7) as its
 * own live state. But 2.8's Data2 (Mode & Time set: 0 stop/1 run/...) is
 * RC's own field to assert, not just an echo - so the UI can set a
 * *desired* state (see requestMode() etc. below) that our B5 reply
 * reports instead of blindly mirroring the last E5. We never transmit
 * out of band though - MAIN is the bus master, so a desired-state change
 * only goes out the next time MAIN actually polls us with D5/E5.
 * 0xC5 (VSP change from a local key press) is the other RC-originated
 * frame but isn't wired to any UI yet.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace plc_protocol {

void init();

// Non-blocking: call every loop() iteration. Drains plc_link bytes,
// resyncs on invalid checksums, updates the cached Snapshot below, and
// after Settings OK / Power, sends one B5 in MAIN's 50ms RX slot (1.3:
// TX 50ms, charge 100ms, RX 50ms).
void poll();

struct Snapshot {
  // 0xD5 - outdoor temp/CO2 MAIN is reporting, fan-error bits
  bool hasD5 = false;
  int outdoorTempC = 0;
  int outdoorCo2Ppm = 0;
  uint8_t d5ErrorFlags = 0; // see header comment table, Bit0..7

  // 0xE5 - live mode/fan-speed/bypass/auto/lock MAIN is commanding
  bool hasE5 = false;
  bool bypass = false;
  uint8_t mode = 0;      // 0 stop,1 run,2 reserve-run,3 reserve-stop,4 -,5 filter-reset
  bool autoMode = false;
  bool locked = false;
  uint8_t fanSpeed = 0;  // 0-3
  // Data2/Data3 raw, undecoded beyond the function-code bits below - Data2
  // cycles 1(unhandled)/2(filter time)/3(reserve-run time)/4(reserve-stop
  // time)/10(function code, decoded into cleanMode/sleepMode below plus
  // the E5-only bits here); Data3 is the corresponding hour value when
  // Data2 is 2-4.
  uint8_t timeMode = 0;
  uint8_t timeValue = 0;
  // Only meaningful after a frame where timeMode==10 (function code byte).
  // cleanMode/sleepMode below (shared with D7/E7) get updated from here too.
  bool fireAlarm = false;
  bool heatEnabled = false;
  bool dustRemoval = false;
  bool bathInterlock = false;
  bool hoodInterlock = false;

  // 0xD6 - heater running state + filter wear. heaterRunning is also
  // reported via E5's function-code byte above - either frame updates
  // this same field.
  bool hasD6 = false;
  bool heaterRunning = false;
  int filterRemainingHoursX100 = 0;
  // Spec just says "필터사용% 상위/하위" (filter-usage% upper/lower) with no
  // worked example (unlike CO2's documented /100 and %100 split) - stored
  // as the two raw bytes rather than guessing a combining formula.
  uint8_t filterUsagePercentHi = 0;
  uint8_t filterUsagePercentLo = 0;

  // 0xD7 / 0xE7 - room-controller function bits MAIN is setting
  bool hasD7 = false;
  bool heaterEnabled = false;
  bool cleanMode = false;
  bool exhaustMode = false;
  bool supplyMode = false;
  bool sleepMode = false;
  bool d7Auto = false;

  // 0xD8 - indoor temp/humidity/PM2.5/PM10 (MAIN relaying its own sensor)
  bool hasD8 = false;
  int indoorTempC = 0;
  int indoorHumidityPct = 0;
  int pm25 = 0;
  int pm10 = 0;

  // 0xD9 - TVOC + a second temp/humidity reading
  bool hasD9 = false;
  int tvoc = 0;
  int d9TempC = 0;
  int d9HumidityPct = 0;

  // 0xA6 - VSP (fan voltage curve), informational only on this board
  bool hasA6 = false;
  uint8_t vspMode = 0;
  uint8_t vspLowX2 = 0, vspMidX2 = 0, vspHighX2 = 0;
};

const Snapshot &snapshot();

// Milliseconds since the last frame that passed header + checksum checks
// (time since boot if none yet). Used for the "no connection" indicator.
uint32_t plcMsSinceLastFrame();

// UI-driven desired state - see header comment. Each call seeds from the
// current snapshot the first time it's used (so we don't assert zeros
// before ever hearing from MAIN), then updates just that one field. Takes
// effect in our B5 reply on MAIN's next D5/E5 poll; there is no
// synchronous "sent/failed" result the way the old RS-485 calls had,
// since we never transmit until MAIN asks.
void requestMode(uint8_t mode); // 0 stop, 1 run - 2/3 (reserve) not exposed in UI yet
void requestBypass(bool on);
void requestAutoMode(bool on);
void requestLocked(bool on);
void requestFanSpeed(uint8_t speed); // 0-3
void requestCleanMode(bool on);

// --- Diagnostic only, not part of the normal request/reply flow -----------
// Sends ONE syntactically valid B5 frame (device id 0x00, all data bytes
// 0x00) through the same low-level plc_link::sendBytes() path replyB5()
// uses internally - but bypassing replyB5() and poll()/decode entirely, so
// a single isolated TX can be correlated against whatever (if anything)
// shows up in the RX log right after it, without waiting on a successful
// decode (hasn't happened this session) or blending into repeated real
// replies. Call this exactly once (e.g. one delayed call after boot) - it
// is NOT meant to be looped or wired into poll().
void sendDiagnosticProbeOnce();

// What we're CURRENTLY asserting (or will, once a request*() above has
// been called at least once) - for the UI to show its own pending state
// rather than just MAIN's last-reported one.
struct Desired {
  bool valid = false; // false until the first request*() or E5 seeds it
  uint8_t mode = 0;
  bool bypass = false;
  bool autoMode = false;
  bool locked = false;
  uint8_t fanSpeed = 0;
  bool cleanMode = false;
};
const Desired &desired();

} // namespace plc_protocol
