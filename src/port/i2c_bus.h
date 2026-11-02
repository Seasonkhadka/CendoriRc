/*
 * Shared I2C master bus (SDA=40, SCL=41), one driver instance for both
 * sensors.cpp and touch_buttons.cpp - same role as Wire.begin() used to
 * play, just over ESP-IDF's driver/i2c.h instead.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace i2c_bus {

void begin(int sda, int scl, uint32_t freqHz);

// addr + data, STOP. Matches Wire: beginTransmission/write.../endTransmission().
bool writeBytes(uint8_t addr, const uint8_t *data, size_t len);

// addr + zero-length write, just checks for an ACK. Matches
// Wire: beginTransmission/endTransmission() with no write() in between.
bool probe(uint8_t addr);

// Pure read transaction (own START/STOP, no preceding write in the same
// transaction). Matches Wire.requestFrom() called on its own. Returns the
// number of bytes actually read (0 on NACK/error).
int readBytes(uint8_t addr, uint8_t *buf, int n);

// Write then repeated-START read, no STOP in between. Matches
// Wire: endTransmission(false) immediately followed by requestFrom().
// Returns the number of bytes actually read (0 on NACK/error).
int writeThenRead(uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, int rlen);

} // namespace i2c_bus
