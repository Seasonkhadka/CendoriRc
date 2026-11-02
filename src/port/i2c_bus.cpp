#include "i2c_bus.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

namespace i2c_bus {

namespace {
constexpr i2c_port_t kPort = I2C_NUM_0;
constexpr TickType_t kTimeout = pdMS_TO_TICKS(100);
} // namespace

void begin(int sda, int scl, uint32_t freqHz) {
  i2c_config_t cfg = {};
  cfg.mode = I2C_MODE_MASTER;
  cfg.sda_io_num = (gpio_num_t)sda;
  cfg.scl_io_num = (gpio_num_t)scl;
  cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.master.clk_speed = freqHz;
  i2c_param_config(kPort, &cfg);
  i2c_driver_install(kPort, I2C_MODE_MASTER, 0, 0, 0);
}

bool writeBytes(uint8_t addr, const uint8_t *data, size_t len) {
  return i2c_master_write_to_device(kPort, addr, data, len, kTimeout) == ESP_OK;
}

bool probe(uint8_t addr) {
  // i2c_master_write() hard-requires a non-null data pointer even for a
  // zero-length write (it only ever dereferences up to `size` bytes of
  // it, so an unused byte is fine) - Wire.endTransmission() with no
  // preceding write() calls allowed a true null/empty write, this doesn't.
  uint8_t unused = 0;
  return i2c_master_write_to_device(kPort, addr, &unused, 0, kTimeout) == ESP_OK;
}

int readBytes(uint8_t addr, uint8_t *buf, int n) {
  if (n <= 0) return 0;
  return i2c_master_read_from_device(kPort, addr, buf, (size_t)n, kTimeout) == ESP_OK ? n : 0;
}

int writeThenRead(uint8_t addr, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, int rlen) {
  if (rlen <= 0) return 0;
  return i2c_master_write_read_device(kPort, addr, wbuf, wlen, rbuf, (size_t)rlen, kTimeout) == ESP_OK
             ? rlen
             : 0;
}

} // namespace i2c_bus
