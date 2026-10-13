#include "plc_link.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_wrap_reg.h"

namespace plc_link {

namespace {

constexpr int kPinTx = 19; // PLC-TX -> Q5 (must stay LOW while we only listen)
constexpr int kPinRx = 20; // PLC-RX <- Q3
constexpr uint32_t kBaud = 2400;
constexpr uint32_t kFrameGapMs = 30;
constexpr int64_t kLinkTimeoutUs = 2000 * 1000; // 2s - a few idle poll cycles

uint32_t s_lastByteAt = 0;
bool s_lineOpen = false;
int64_t s_lastByteAtUs = 0; // esp_timer time base, independent of millis()

} // namespace

void holdQ5Off() {
  // ROM enables USB Serial/JTAG on GPIO19/20 (D-/D+). That pad is the
  // same net as Q5; leave it claimed and PLC 5V collapses, LCD stays dark.
  SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_PAD_ENABLE_OVERRIDE);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_WRAP_OTG_CONF_REG, USB_WRAP_USB_PAD_ENABLE);

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPinTx;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  gpio_set_level((gpio_num_t)kPinTx, 0);
}

namespace {
struct EarlyQ5Off {
  EarlyQ5Off() { holdQ5Off(); }
} s_earlyQ5Off;
} // namespace

void init() {
  holdQ5Off();

  Serial2.begin(kBaud, SERIAL_8N1, kPinRx, -1);
  Serial.printf("[PLC] listen-only UART2 2400 RX=GPIO%d; GPIO%d LOW (Q5 off)\n",
                kPinRx, kPinTx);
}

void poll() {
  while (Serial2.available()) {
    uint32_t now = millis();
    if (s_lineOpen && now - s_lastByteAt > kFrameGapMs) {
      Serial.println();
      s_lineOpen = false;
    }
    if (!s_lineOpen) {
      Serial.printf("[PLC] t=%lums:", (unsigned long)now);
      s_lineOpen = true;
    }
    Serial.printf(" %02X", Serial2.read());
    s_lastByteAt = now;
    s_lastByteAtUs = esp_timer_get_time();
  }
  if (s_lineOpen && millis() - s_lastByteAt > kFrameGapMs) {
    Serial.println();
    s_lineOpen = false;
  }
}

bool isConnected() {
  return s_lastByteAtUs != 0 && (esp_timer_get_time() - s_lastByteAtUs) < kLinkTimeoutUs;
}

} // namespace plc_link
