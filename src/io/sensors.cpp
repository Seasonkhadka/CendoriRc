#include "sensors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <VOCGasIndexAlgorithm.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "port/co_adc.h"
#include "port/esp_time.h"
#include "port/i2c_bus.h"

namespace sensors {

namespace {

constexpr uint8_t kAddrSHT40 = 0x44;
constexpr uint8_t kAddrSGP40 = 0x59;
// SCD4x datasheet address is 0x62. 0x64 was used before but never
// produced a sample; detectScd40() picks whichever answers a CRC-valid
// serial-number read, so another chip that merely ACKs can't be mistaken
// for it.
constexpr uint8_t kScd40Candidates[] = {0x62, 0x64};
uint8_t kAddrSCD40 = 0x62;
bool s_scd40Found = false;
constexpr uint8_t kAddrRTC = 0x6F;

// ---- TVOC: SGP40 VOC Index -> ug/m3 (ERV 내부 프로토콜 B6/D9 = ug/m3) -----
// Sensirion VOC Index -> TVOC(ppb) 변환식 (Sensirion "VOC Index to TVOC"
// 안내 식): ppb = (ln(501 - index) - A) * B. ppb -> ug/m3 는 실내 혼합
// 가스(Molhave) 기준 4.5 배. index 100(평상) ~ 94ppb ~ 420 ug/m3.
// 미확인 - 현장 값과 비교 후 조정.
#define SEN_TVOC_LN_OFFSET		6.24f
#define SEN_TVOC_PPB_SCALE		(-381.97f)
#define SEN_TVOC_UG_PER_PPB		4.5f
#define SEN_TVOC_MAX_UG			65535	// B6 Data3/4 16-bit

// ---- CO: ME2-CO 전기화학 셀 (회로도 CD-WD-RMC-V1.0 "CO SENSOR") -----------
// VREF = 5V * R51 / (R50 + R51) = 5V * 100k / 1.1M = 455mV (U13B 버퍼)
// SEN_VOUT = VREF + I_cell * Rf, Rf = R53 + (R54 // RT1) = 100k + (300k // 50k)
//          = 142.9k (25도, NTC 가 셀 온도 특성 보상)
// ppm = (mV - zero mV) / (감도 uA/ppm * Rf kOhm)
#define SEN_CO_VREF_MV			455		// 영점 초기값 (회로 계산값, 저항 오차 +-5%)
#define SEN_CO_RF_KOHM			142.86f
#define SEN_CO_SENS_UA_PER_PPM	0.02f	// ME2-CO 감도 - 미확인, 셀 데이터시트/교정으로 확인
#define SEN_CO_AUTO_ZERO		1		// 1: 부팅 후 가장 낮은 평균 전압을 0ppm 으로 학습
#define SEN_CO_WARMUP_MS		60000	// 셀 안정화 시간 - 이 동안 값 표시 안 함, 영점 학습 안 함
#define SEN_CO_OVERSAMPLE		16		// 1회 측정 당 ADC 읽기 횟수 (평균)
#define SEN_CO_EMA_DIV			8		// 이동평균 (1/8) - ADC 잡음 제거

// ---- Sensirion CRC-8 (poly 0x31, init 0xFF) - shared by SHT40/SGP40/
// SCD40 only. The RTC is a plain BCD register map with no CRC at all. --
uint8_t sensirionCrc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// ---- tiny I2C helpers --------------------------------------------------

bool writeCmd8(uint8_t addr, uint8_t cmd) {
  return i2c_bus::writeBytes(addr, &cmd, 1);
}

bool writeCmd16(uint8_t addr, uint16_t cmd) {
  uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
  return i2c_bus::writeBytes(addr, buf, 2);
}

int readBytes(uint8_t addr, uint8_t *buf, int n) {
  return i2c_bus::readBytes(addr, buf, n);
}

uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Boot-time diagnostic: lists every address that ACKs on the shared bus,
// so wiring/power problems show up immediately instead of looking like a
// protocol bug. Expected: 0x24 (touch IC), 0x44 (SHT40), 0x59 (SGP40),
// 0x62 (SCD40 per datasheet), 0x6F (RTC). 0x28 (PM2012) only shows up once pin 8/P has
// been grounded - see kPm2012HardwareReady below.
void scanBus() {
  printf("[I2C] scan GPIO40/41:");
  int n = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (!i2c_bus::probe(addr)) continue;
    printf(" %02X", addr);
    n++;
  }
  printf(n ? " (%d ACK)\n" : " none\n", n);
}

// ---- cache ---------------------------------------------------------

struct Cache {
  float tempC = 0, humidityPct = 0;
  bool tempValid = false;

  int co2Ppm = 0;
  bool co2Valid = false;

  uint16_t vocRaw = 0;
  int vocIndex = 0;  // 0 during the algorithm's ~45 s start-up blackout
  int tvocPpb = 0;
  int tvocUg = 0;
  bool vocValid = false;

  int hour = 0, minute = 0, second = 0, day = 1, month = 1, year = 2000;
  bool rtcValid = false;

  int coMilliVolts = 0;   // filtered SEN_VOUT
  float coZeroMv = SEN_CO_VREF_MV;
  float coPpm = 0;
  bool coValid = false;

  int pm1Ug = 0, pm25Ug = 0, pm10Ug = 0;
  bool pmValid = false;
};
Cache s_cache;

int s_coPin = -1;
bool s_scd40Started = false;

// Sensirion's official VOC Index Algorithm (open-source, not reimplemented
// here): a stateful adaptive filter, not a simple formula, that turns raw
// SGP40 ticks into the 0-500 index the datasheet/UI expect (100 = normal
// baseline air). Defaults to a 1s sampling interval internally, which
// matches kIntervalSgp40 below - call process() once per actual SGP40
// reading, not more/less often, or its internal adaptation will be off.
VOCGasIndexAlgorithm s_vocAlgorithm;

// ---- SHT40 (temp / humidity) - single-byte command ---------------------

bool readSHT40() {
  if (!writeCmd8(kAddrSHT40, 0xFD)) { // high-precision measurement
    return false;
  }
  delay(10); // datasheet: <=8.3ms, +margin

  uint8_t buf[6];
  int got = readBytes(kAddrSHT40, buf, 6);
  if (got != 6) return false;
  if (sensirionCrc8(buf, 2) != buf[2] || sensirionCrc8(buf + 3, 2) != buf[5]) {
    return false;
  }

  uint16_t tRaw = (uint16_t)((buf[0] << 8) | buf[1]);
  uint16_t rhRaw = (uint16_t)((buf[3] << 8) | buf[4]);
  s_cache.tempC = -45.0f + 175.0f * ((float)tRaw / 65535.0f);
  float rh = -6.0f + 125.0f * ((float)rhRaw / 65535.0f);
  if (rh < 0) rh = 0;
  if (rh > 100) rh = 100;
  s_cache.humidityPct = rh;
  s_cache.tempValid = true;
  return true;
}

// ---- SGP40 (VOC raw signal) - 16-bit command + compensation params -----
// Humidity / temperature compensation uses the latest SHT40 reading (the
// datasheet defaults 50% RH / 25 degC until the first SHT40 sample).
// Without it the raw signal moves with room humidity, which the VOC
// Index then reports as VOC changes.

int tvocPpbFromIndex(int index) {
  if (index <= 0) return 0;
  if (index > 500) index = 500;
  float ppb = (logf(501.0f - (float)index) - SEN_TVOC_LN_OFFSET) * SEN_TVOC_PPB_SCALE;
  if (ppb < 0) ppb = 0;
  long v = lroundf(ppb);
  return v > 65535 ? 65535 : (int)v;
}

int tvocUgFromPpb(int ppb) {
  long ug = lroundf((float)ppb * SEN_TVOC_UG_PER_PPB);
  return ug > SEN_TVOC_MAX_UG ? SEN_TVOC_MAX_UG : (int)ug;
}

bool readSGP40() {
  uint16_t rhT = 0x8000, tT = 0x6666;
  if (s_cache.tempValid) {
    float t = s_cache.tempC < -45.0f ? -45.0f : (s_cache.tempC > 130.0f ? 130.0f : s_cache.tempC);
    rhT = (uint16_t)lroundf(s_cache.humidityPct * 65535.0f / 100.0f);
    tT = (uint16_t)lroundf((t + 45.0f) * 65535.0f / 175.0f);
  }
  const uint8_t rhTicks[2] = {(uint8_t)(rhT >> 8), (uint8_t)(rhT & 0xFF)};
  const uint8_t tTicks[2] = {(uint8_t)(tT >> 8), (uint8_t)(tT & 0xFF)};
  uint8_t rhCrc = sensirionCrc8(rhTicks, 2);
  uint8_t tCrc = sensirionCrc8(tTicks, 2);

  uint8_t cmd[8] = {0x26, 0x0F, rhTicks[0], rhTicks[1], rhCrc, tTicks[0], tTicks[1], tCrc};
  if (!i2c_bus::writeBytes(kAddrSGP40, cmd, sizeof(cmd))) {
    return false;
  }

  delay(30); // datasheet: <=30ms

  uint8_t buf[3];
  int got = readBytes(kAddrSGP40, buf, 3);
  if (got != 3) return false;
  if (sensirionCrc8(buf, 2) != buf[2]) return false;

  s_cache.vocRaw = (uint16_t)((buf[0] << 8) | buf[1]);
  s_cache.vocIndex = (int)s_vocAlgorithm.process((int32_t)s_cache.vocRaw);
  // The algorithm returns 0 until its start-up blackout ends - show "--"
  // instead of a fake 0 followed by a jump.
  if (s_cache.vocIndex <= 0) return false;
  s_cache.tvocPpb = tvocPpbFromIndex(s_cache.vocIndex);
  s_cache.tvocUg = tvocUgFromPpb(s_cache.tvocPpb);
  s_cache.vocValid = true;
  return true;
}

// ---- SCD40 (CO2) - periodic measurement, self-paced at 5s --------------

void logScd40Fail(const char *why) {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 5000) return;
  lastMs = now;
  printf("[SEN] SCD40 fail: %s started=%d\n", why, s_scd40Started ? 1 : 0);
}

// get_serial_number (0x3682) only works while idle - call after stop.
bool detectScd40() {
  for (uint8_t addr : kScd40Candidates) {
    if (!writeCmd16(addr, 0x3682)) continue;
    delay(2);
    uint8_t b[9];
    if (readBytes(addr, b, 9) != 9) continue;
    if (sensirionCrc8(b, 2) != b[2] || sensirionCrc8(b + 3, 2) != b[5] ||
        sensirionCrc8(b + 6, 2) != b[8]) {
      printf("[SEN] SCD4x probe 0x%02X: ACK but bad CRC (not an SCD4x?)\n", addr);
      continue;
    }
    kAddrSCD40 = addr;
    printf("[SEN] SCD4x found at 0x%02X serial=%02X%02X%02X%02X%02X%02X\n", addr, b[0], b[1],
           b[3], b[4], b[6], b[7]);
    return true;
  }
  printf("[SEN] SCD4x NOT FOUND at 0x62/0x64 - CO2 will stay -1\n");
  return false;
}

bool readSCD40() {
  if (!s_scd40Found) return false;
  static uint32_t lastSampleMs = 0;
  if (!s_scd40Started) {
    static uint32_t lastTryMs = 0;
    uint32_t now = millis();
    if (now - lastTryMs >= 5000) {
      lastTryMs = now;
      bool ack = i2c_bus::probe(kAddrSCD40);
      s_scd40Started = writeCmd16(kAddrSCD40, 0x21B1);
      printf("[SEN] SCD40 retry probe%02X=%d start=%s\n", kAddrSCD40, ack ? 1 : 0,
             s_scd40Started ? "OK" : "FAIL");
    }
    if (!s_scd40Started) return false;
  }
  if (!writeCmd16(kAddrSCD40, 0xE4B8)) {
    logScd40Fail("ready cmd NACK");
    return false;
  }
  delay(1);
  uint8_t ready[3];
  if (readBytes(kAddrSCD40, ready, 3) != 3) {
    logScd40Fail("ready read NACK");
    return false;
  }
  if (sensirionCrc8(ready, 2) != ready[2]) {
    logScd40Fail("ready CRC");
    return false;
  }
  uint16_t status = (uint16_t)((ready[0] << 8) | ready[1]);
  if ((status & 0x07FF) == 0) { // not due yet - normal for ~5s, a fault if it never ends
    if (lastSampleMs == 0) lastSampleMs = millis();
    if (millis() - lastSampleMs > 15000) logScd40Fail("no data ready for >15s");
    return false;
  }

  if (!writeCmd16(kAddrSCD40, 0xEC05)) {
    logScd40Fail("data cmd NACK");
    return false;
  }
  delay(1);

  uint8_t buf[9];
  int got = readBytes(kAddrSCD40, buf, 9);
  if (got != 9) {
    logScd40Fail("data read NACK");
    return false;
  }
  if (sensirionCrc8(buf, 2) != buf[2] || sensirionCrc8(buf + 3, 2) != buf[5] ||
      sensirionCrc8(buf + 6, 2) != buf[8]) {
    logScd40Fail("data CRC");
    return false;
  }

  s_cache.co2Ppm = (buf[0] << 8) | buf[1];
  s_cache.co2Valid = true;
  lastSampleMs = millis();
  printf("[SEN] SCD40 CO2=%d ppm\n", s_cache.co2Ppm);
  return true;
}

// ---- MCP7940 RTC - plain BCD registers, no CRC --------------------------

bool rtcEnsureRunning() {
  uint8_t reg = 0x00; // RTCSEC
  uint8_t sec;
  if (i2c_bus::writeThenRead(kAddrRTC, &reg, 1, &sec, 1) != 1) return false;

  if (!(sec & 0x80)) { // ST bit clear -> oscillator not started (fresh chip)
    uint8_t buf[2] = {0x00, (uint8_t)(sec | 0x80)};
    i2c_bus::writeBytes(kAddrRTC, buf, 2);
  }
  return true;
}

bool readRTC() {
  uint8_t reg = 0x00;
  uint8_t buf[7];
  int got = i2c_bus::writeThenRead(kAddrRTC, &reg, 1, buf, 7);
  if (got != 7) return false;

  s_cache.second = bcd2dec(buf[0] & 0x7F);
  s_cache.minute = bcd2dec(buf[1] & 0x7F);

  uint8_t hourReg = buf[2];
  if (hourReg & 0x40) { // 12-hour mode
    int h = bcd2dec(hourReg & 0x1F);
    if (hourReg & 0x20) { // PM
      if (h != 12) h += 12;
    } else {
      if (h == 12) h = 0;
    }
    s_cache.hour = h;
  } else { // 24-hour mode
    s_cache.hour = bcd2dec(hourReg & 0x3F);
  }

  s_cache.day = bcd2dec(buf[4] & 0x3F);
  s_cache.month = bcd2dec(buf[5] & 0x1F);
  s_cache.year = 2000 + bcd2dec(buf[6]);
  s_cache.rtcValid = true;
  return true;
}

// Firmware build time (the build PC's local time). scripts/stamp_build_time.py
// touches this file before every build so __DATE__/__TIME__ stay current.
struct DateTime {
  int year, month, day, hour, minute, second;
};

DateTime buildTime() {
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  memcpy(mon, __DATE__, 3);
  const char *p = strstr(kMonths, mon);
  DateTime t;
  t.month = p ? (int)(p - kMonths) / 3 + 1 : 1;
  t.day = atoi(__DATE__ + 4);
  t.year = atoi(__DATE__ + 7);
  t.hour = atoi(__TIME__);
  t.minute = atoi(__TIME__ + 3);
  t.second = atoi(__TIME__ + 6);
  return t;
}

// Comparable minute stamp, yyyymmddhhmm.
long long stamp(const DateTime &t) {
  return ((((long long)t.year * 100 + t.month) * 100 + t.day) * 100 + t.hour) * 100 + t.minute;
}

// 1 = Monday ... 7 = Sunday (Sakamoto).
int weekday(int y, int m, int d) {
  static const int kOffset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  int w = (y + y / 4 - y / 100 + y / 400 + kOffset[m - 1] + d) % 7; // 0 = Sunday
  return w == 0 ? 7 : w;
}

// MCP7940: stop the oscillator, write time/date in 24-hour mode with the
// battery backup enabled (VBATEN), then start it again.
bool rtcWrite(const DateTime &t) {
  uint8_t stop[2] = {0x00, 0x00};
  if (!i2c_bus::writeBytes(kAddrRTC, stop, sizeof(stop))) return false;
  for (int i = 0; i < 10; i++) { // wait for OSCRUN (RTCWKDAY bit 5) to clear
    uint8_t reg = 0x03, wk = 0;
    if (i2c_bus::writeThenRead(kAddrRTC, &reg, 1, &wk, 1) == 1 && !(wk & 0x20)) break;
    delay(1);
  }
  uint8_t regs[7] = {0x01,
                     dec2bcd(t.minute),
                     dec2bcd(t.hour),
                     (uint8_t)(0x08 | weekday(t.year, t.month, t.day)),
                     dec2bcd(t.day),
                     dec2bcd(t.month),
                     dec2bcd(t.year % 100)};
  if (!i2c_bus::writeBytes(kAddrRTC, regs, sizeof(regs))) return false;
  uint8_t start[2] = {0x00, (uint8_t)(0x80 | dec2bcd(t.second))};
  return i2c_bus::writeBytes(kAddrRTC, start, sizeof(start));
}

// True the first time a given firmware build boots (its __DATE__ __TIME__
// differs from the one saved in NVS); saves the new one.
bool isNewBuild() {
  static const char kBuild[] = __DATE__ " " __TIME__;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) return false;
  nvs_handle_t h;
  if (nvs_open("rtc", NVS_READWRITE, &h) != ESP_OK) return false;
  char saved[sizeof(kBuild)] = {};
  size_t len = sizeof(saved);
  const bool same = nvs_get_str(h, "build", saved, &len) == ESP_OK && strcmp(saved, kBuild) == 0;
  if (!same) {
    nvs_set_str(h, "build", kBuild);
    nvs_commit(h);
  }
  nvs_close(h);
  return !same;
}

// The RTC is set to the firmware build time (the build PC's clock) on the
// first boot of every new build, whatever it held before, and on any boot
// where it is behind the build time (never set, or lost power without a
// battery). Otherwise the running RTC is kept.
void rtcSetIfBehindBuild() {
  if (!readRTC()) {
    printf("[SEN] RTC read FAIL (no MCP7940 at 0x%02X?)\n", kAddrRTC);
    return;
  }
  const DateTime rtc = {s_cache.year, s_cache.month, s_cache.day, s_cache.hour, s_cache.minute, s_cache.second};
  const DateTime build = buildTime();
  const bool newBuild = isNewBuild();
  if (!newBuild && stamp(rtc) >= stamp(build)) {
    printf("[SEN] RTC %04d-%02d-%02d %02d:%02d OK\n", rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute);
    return;
  }
  const bool ok = rtcWrite(build);
  printf("[SEN] RTC %04d-%02d-%02d %02d:%02d %s -> set %04d-%02d-%02d %02d:%02d:%02d %s\n", rtc.year,
         rtc.month, rtc.day, rtc.hour, rtc.minute, newBuild ? "new build" : "behind build time", build.year,
         build.month, build.day, build.hour, build.minute, build.second, ok ? "OK" : "FAIL");
  readRTC();
}

// ---- CO (analog, via the op-amp conditioning circuit on GPIO8) --------

bool readCO() {
  if (s_coPin < 0) return false;
  int sum = 0;
  for (int i = 0; i < SEN_CO_OVERSAMPLE; i++) {
    int mv = co_adc::readMilliVolts();
    if (mv < 0) return false;
    sum += mv;
  }
  const float mv = (float)sum / SEN_CO_OVERSAMPLE;

  static float s_ema = -1;
  static uint32_t s_startMs = 0;
  if (s_ema < 0) {
    s_ema = mv;
    s_startMs = millis();
  } else {
    s_ema += (mv - s_ema) / SEN_CO_EMA_DIV;
  }
  s_cache.coMilliVolts = (int)lroundf(s_ema);
  if (millis() - s_startMs < SEN_CO_WARMUP_MS) return false;

#if SEN_CO_AUTO_ZERO
  if (s_ema < s_cache.coZeroMv) s_cache.coZeroMv = s_ema;
#endif
  float ppm = (s_ema - s_cache.coZeroMv) / (SEN_CO_SENS_UA_PER_PPM * SEN_CO_RF_KOHM);
  s_cache.coPpm = ppm < 0 ? 0 : ppm;
  s_cache.coValid = true;
  return true;
}

// ---- PM2012 (PM1.0/PM2.5/PM10 dust) -------------------------------------
// Confirmed via the Cubic PM2012 datasheet S3.2 "Read Data Command": the
// I2C read is just 32 raw bytes from 0x28, no command byte first. Frame
// layout, byte offsets and the checksum below are taken directly from
// that section (the UART-mode checksum is a different algorithm - a
// 256-sum complement - and must never be used here; this is the XOR one).
//
//   b[0]      header, always 0x16
//   b[1]      frame length, always 32
//   b[2]      status: 0x02 = measuring (normal in continuous mode), 0x80 =
//             data stable (only reached once PM1.0 < 5 ug/m3 and the laser
//             pauses) - both are valid readings; 0x01 closed / 0x07 alarm
//             are not.
//   b[7..8]   PM1.0  ug/m3, big-endian
//   b[9..10]  PM2.5  ug/m3, big-endian
//   b[11..12] PM10   ug/m3, big-endian
//   b[31]     check code = b[0] ^ b[1] ^ ... ^ b[30]
//
// kPm2012HardwareReady is false by default: the module currently defaults
// to UART mode (pin 8 / P left floating), and in that mode its UART RX
// sits on the same wire as I2C SDA, so touching 0x28 risks colliding with
// the other sensors on the bus. Flip this to true only once pin 8 has
// been bodge-wired to GND to force I2C mode (leave pin 10 / SET floating -
// that one's sleep/wake, a different pin with a different purpose).
constexpr bool kPm2012HardwareReady = true; // 0x28 now ACKs a real I2C scan - see sensors.h
constexpr uint8_t kAddrPM2012 = 0x28;

bool readPM2012() {
  uint8_t b[32];
  if (readBytes(kAddrPM2012, b, 32) != 32) return false;
  if (b[0] != 0x16 || b[1] != 32) return false; // header / frame length

  uint8_t chk = 0;
  for (int i = 0; i < 31; i++) chk ^= b[i];
  if (chk != b[31]) return false;

  if (b[2] != 0x02 && b[2] != 0x80) return false; // 0x01 closed, 0x07 alarm -> reject

  s_cache.pm1Ug = (b[7] << 8) | b[8];
  s_cache.pm25Ug = (b[9] << 8) | b[10];
  s_cache.pm10Ug = (b[11] << 8) | b[12];
  s_cache.pmValid = true;
  return true;
}

// ---- round-robin scheduler ------------------------------------------

enum class Step { Sht40, Sgp40, Scd40, Rtc, Co, Pm2012, Count };
int s_step = 0;
uint32_t s_lastSht40 = 0, s_lastSgp40 = 0, s_lastScd40 = 0, s_lastRtc = 0, s_lastCo = 0;
uint32_t s_lastPm2012 = 0;

constexpr uint32_t kIntervalSht40 = 2000;
constexpr uint32_t kIntervalSgp40 = 1000;
constexpr uint32_t kIntervalScd40Check = 1000; // how often we ask "ready yet?" - the
                                                // sensor itself only actually has new
                                                // data roughly every 5s regardless.
constexpr uint32_t kIntervalRtc = 1000;
constexpr uint32_t kIntervalCo = 1000;
constexpr uint32_t kIntervalPm2012 = 2000;

} // namespace

void init(int coAdcPin) {
  // i2c_bus::begin() is called once in main.cpp, shared with the touch IC.
  scanBus();

  s_coPin = coAdcPin;
  co_adc::init(s_coPin);

  // Unlike the other sensors, the SCD40 has internal mode state that
  // survives an MCU reset (an EN-pin reset / re-upload doesn't power-cycle
  // the sensor). If a previous boot left it running periodic measurement,
  // sending "start" again is ignored and s_scd40Started would stay false
  // forever - CO2 would never update even though everything else does. So
  // always stop first and wait out its settle time before starting clean.
  for (uint8_t addr : kScd40Candidates) writeCmd16(addr, 0x3F86); // stop_periodic_measurement
  delay(500); // datasheet: up to 500ms before it accepts new commands
  s_scd40Found = detectScd40();
  if (s_scd40Found) {
    s_scd40Started = writeCmd16(kAddrSCD40, 0x21B1); // start periodic measurement
    printf("[SEN] SCD4x start %s\n", s_scd40Started ? "OK" : "FAIL");
  }
  // Oscillator on, but do not write build time - the user sets date/time
  // in Settings -> 시간 설정. NTP may still correct it after Wi-Fi connects.
  if (rtcEnsureRunning()) readRTC();
}

bool poll() {
  uint32_t now = millis();
  bool changed = false;

  switch ((Step)s_step) {
    case Step::Sht40:
      if (now - s_lastSht40 >= kIntervalSht40) {
        s_lastSht40 = now;
        changed = readSHT40();
      }
      break;
    case Step::Sgp40:
      if (now - s_lastSgp40 >= kIntervalSgp40) {
        s_lastSgp40 = now;
        changed = readSGP40();
      }
      break;
    case Step::Scd40:
      if (now - s_lastScd40 >= kIntervalScd40Check) {
        s_lastScd40 = now;
        changed = readSCD40();
      }
      break;
    case Step::Rtc:
      if (now - s_lastRtc >= kIntervalRtc) {
        s_lastRtc = now;
        changed = readRTC();
      }
      break;
    case Step::Co:
      if (now - s_lastCo >= kIntervalCo) {
        s_lastCo = now;
        changed = readCO();
      }
      break;
    case Step::Pm2012:
      if (kPm2012HardwareReady && now - s_lastPm2012 >= kIntervalPm2012) {
        s_lastPm2012 = now;
        changed = readPM2012();
      }
      break;
    default:
      break;
  }

  s_step = (s_step + 1) % (int)Step::Count;
  return changed;
}

bool hasTemp() { return s_cache.tempValid; }
float getTempC() { return s_cache.tempC; }
float getHumidityPct() { return s_cache.humidityPct; }

bool hasCo2() { return s_cache.co2Valid; }
int getCo2Ppm() { return s_cache.co2Ppm; }

bool hasVocRaw() { return s_cache.vocValid; }
uint16_t getVocRaw() { return s_cache.vocRaw; }
int getVocIndex() { return s_cache.vocIndex; }
int getTvocUg() { return s_cache.tvocUg; }
int getTvocPpb() { return s_cache.tvocPpb; }

bool hasRtc() { return s_cache.rtcValid; }

bool setRtcTime(int year, int month, int day, int hour, int minute, int second) {
  const DateTime t = {year, month, day, hour, minute, second};
  if (!rtcWrite(t)) return false;
  return readRTC();
}
int getHour24() { return s_cache.hour; }
int getMinute() { return s_cache.minute; }
int getSecond() { return s_cache.second; }
int getDay() { return s_cache.day; }
int getMonth() { return s_cache.month; }
int getYear() { return s_cache.year; }

bool hasCo() { return s_cache.coValid; }
int getCoMilliVolts() { return s_cache.coMilliVolts; }
float getCoPpm() { return s_cache.coPpm; }

bool hasPm() { return s_cache.pmValid; }
int getPm1Ug() { return s_cache.pm1Ug; }
int getPm25Ug() { return s_cache.pm25Ug; }
int getPm10Ug() { return s_cache.pm10Ug; }

} // namespace sensors
