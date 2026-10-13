#include "sensors.h"
#include <Wire.h>
#include <VOCGasIndexAlgorithm.h>

namespace sensors {

namespace {

constexpr uint8_t kAddrSHT40 = 0x44;
constexpr uint8_t kAddrSGP40 = 0x59;
constexpr uint8_t kAddrSCD40 = 0x64;
constexpr uint8_t kAddrRTC = 0x6F;

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
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool writeCmd16(uint8_t addr, uint16_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

int readBytes(uint8_t addr, uint8_t *buf, int n) {
  int got = Wire.requestFrom((int)addr, n);
  for (int i = 0; i < got && i < n; i++) buf[i] = Wire.read();
  return got;
}

uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

// Boot-time diagnostic: lists every address that ACKs on the shared bus,
// so wiring/power problems show up immediately instead of looking like a
// protocol bug. Expected: 0x24 (touch IC), 0x44 (SHT40), 0x59 (SGP40),
// 0x64 (SCD40), 0x6F (RTC). 0x28 (PM2012) only shows up once pin 8/P has
// been grounded - see kPm2012HardwareReady below.
void scanBus() {
  Serial.println("[I2C] scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   found device at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C] scan done, %d device(s) found\n", found);
}

// ---- cache ---------------------------------------------------------

struct Cache {
  float tempC = 0, humidityPct = 0;
  bool tempValid = false;

  int co2Ppm = 0;
  bool co2Valid = false;

  uint16_t vocRaw = 0;
  int vocIndex = 0;
  bool vocValid = false;

  int hour = 0, minute = 0, second = 0, day = 1, month = 1, year = 2000;
  bool rtcValid = false;

  int coMilliVolts = 0;
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
    Serial.println("[SHT40] command write failed (no ACK - check wiring/power)");
    return false;
  }
  delay(10); // datasheet: <=8.3ms, +margin

  uint8_t buf[6];
  int got = readBytes(kAddrSHT40, buf, 6);
  if (got != 6) {
    Serial.printf("[SHT40] read failed, got %d/6 bytes\n", got);
    return false;
  }
  if (sensirionCrc8(buf, 2) != buf[2] || sensirionCrc8(buf + 3, 2) != buf[5]) {
    Serial.println("[SHT40] CRC mismatch - bad data on the bus");
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
// Compensation is fixed at the datasheet's documented defaults (50% RH /
// 25 degC -> ticks 0x8000 / 0x6666): we don't have a humidity reading
// available synchronously here, and the raw signal is uncalibrated
// either way without Sensirion's Gas Index Algorithm (not ported yet).

bool readSGP40() {
  static const uint8_t rhTicks[2] = {0x80, 0x00};
  static const uint8_t tTicks[2] = {0x66, 0x66};
  uint8_t rhCrc = sensirionCrc8(rhTicks, 2);
  uint8_t tCrc = sensirionCrc8(tTicks, 2);

  Wire.beginTransmission(kAddrSGP40);
  Wire.write(0x26);
  Wire.write(0x0F);
  Wire.write(rhTicks[0]);
  Wire.write(rhTicks[1]);
  Wire.write(rhCrc);
  Wire.write(tTicks[0]);
  Wire.write(tTicks[1]);
  Wire.write(tCrc);
  if (Wire.endTransmission() != 0) {
    Serial.println("[SGP40] command write failed (no ACK - check wiring/power)");
    return false;
  }

  delay(30); // datasheet: <=30ms

  uint8_t buf[3];
  int got = readBytes(kAddrSGP40, buf, 3);
  if (got != 3) {
    Serial.printf("[SGP40] read failed, got %d/3 bytes\n", got);
    return false;
  }
  if (sensirionCrc8(buf, 2) != buf[2]) {
    Serial.println("[SGP40] CRC mismatch - bad data on the bus");
    return false;
  }

  s_cache.vocRaw = (uint16_t)((buf[0] << 8) | buf[1]);
  s_cache.vocIndex = (int)s_vocAlgorithm.process((int32_t)s_cache.vocRaw);
  s_cache.vocValid = true;
  return true;
}

// ---- SCD40 (CO2) - periodic measurement, self-paced at 5s --------------

bool scd40DataReady() {
  if (!writeCmd16(kAddrSCD40, 0xE4B8)) {
    Serial.println("[SCD40] data-ready command write failed (no ACK - check wiring/power)");
    return false;
  }
  delay(1);
  uint8_t buf[3];
  int got = readBytes(kAddrSCD40, buf, 3);
  if (got != 3) {
    Serial.printf("[SCD40] data-ready read failed, got %d/3 bytes\n", got);
    return false;
  }
  if (sensirionCrc8(buf, 2) != buf[2]) {
    Serial.println("[SCD40] data-ready CRC mismatch - bad data on the bus");
    return false;
  }
  uint16_t status = (uint16_t)((buf[0] << 8) | buf[1]);
  return (status & 0x07FF) != 0; // false here just means "not due yet" - normal, not logged
}

bool readSCD40() {
  if (!s_scd40Started) {
    Serial.println("[SCD40] not started - periodic measurement start failed at boot");
    return false;
  }
  if (!scd40DataReady()) return false; // not due yet - SCD40 only updates every ~5s

  if (!writeCmd16(kAddrSCD40, 0xEC05)) {
    Serial.println("[SCD40] measurement command write failed (no ACK)");
    return false;
  }
  delay(1);

  uint8_t buf[9];
  int got = readBytes(kAddrSCD40, buf, 9);
  if (got != 9) {
    Serial.printf("[SCD40] measurement read failed, got %d/9 bytes\n", got);
    return false;
  }
  if (sensirionCrc8(buf, 2) != buf[2] || sensirionCrc8(buf + 3, 2) != buf[5] ||
      sensirionCrc8(buf + 6, 2) != buf[8]) {
    Serial.println("[SCD40] measurement CRC mismatch - bad data on the bus");
    return false;
  }

  s_cache.co2Ppm = (buf[0] << 8) | buf[1];
  s_cache.co2Valid = true;
  // SCD40 also reports its own temp/humidity here, but we use SHT40 for
  // that (one consistent source for the UI) and ignore SCD40's copy.
  return true;
}

// ---- MCP7940 RTC - plain BCD registers, no CRC --------------------------

bool rtcEnsureRunning() {
  Wire.beginTransmission(kAddrRTC);
  Wire.write((uint8_t)0x00); // RTCSEC
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t sec;
  if (readBytes(kAddrRTC, &sec, 1) != 1) return false;

  if (!(sec & 0x80)) { // ST bit clear -> oscillator not started (fresh chip)
    Wire.beginTransmission(kAddrRTC);
    Wire.write((uint8_t)0x00);
    Wire.write((uint8_t)(sec | 0x80));
    Wire.endTransmission();
  }
  return true;
}

bool readRTC() {
  Wire.beginTransmission(kAddrRTC);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) {
    Serial.println("[RTC] register pointer write failed (no ACK - check wiring/power)");
    return false;
  }

  uint8_t buf[7];
  int got = readBytes(kAddrRTC, buf, 7);
  if (got != 7) {
    Serial.printf("[RTC] read failed, got %d/7 bytes\n", got);
    return false;
  }

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

// ---- CO (analog, via the op-amp conditioning circuit on GPIO8) --------

bool readCO() {
  if (s_coPin < 0) return false;
  s_cache.coMilliVolts = analogReadMilliVolts(s_coPin);
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
  // Wire.begin() is called once in main.cpp, shared with the touch IC.
  scanBus();

  s_coPin = coAdcPin;
  pinMode(s_coPin, INPUT);

  // Unlike the other sensors, the SCD40 has internal mode state that
  // survives an MCU reset (an EN-pin reset / re-upload doesn't power-cycle
  // the sensor). If a previous boot left it running periodic measurement,
  // sending "start" again is ignored and s_scd40Started would stay false
  // forever - CO2 would never update even though everything else does. So
  // always stop first and wait out its settle time before starting clean.
  writeCmd16(kAddrSCD40, 0x3F86); // stop_periodic_measurement
  delay(500);                    // datasheet: up to 500ms before it accepts new commands
  s_scd40Started = writeCmd16(kAddrSCD40, 0x21B1); // start periodic measurement
  rtcEnsureRunning();
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

bool hasRtc() { return s_cache.rtcValid; }
int getHour24() { return s_cache.hour; }
int getMinute() { return s_cache.minute; }
int getSecond() { return s_cache.second; }
int getDay() { return s_cache.day; }
int getMonth() { return s_cache.month; }
int getYear() { return s_cache.year; }

bool hasCo() { return s_cache.coValid; }
int getCoMilliVolts() { return s_cache.coMilliVolts; }

bool hasPm() { return s_cache.pmValid; }
int getPm1Ug() { return s_cache.pm1Ug; }
int getPm25Ug() { return s_cache.pm25Ug; }
int getPm10Ug() { return s_cache.pm10Ug; }

} // namespace sensors
