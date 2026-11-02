#include "plc_link.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_wrap_reg.h"

namespace plc_link {

namespace {

constexpr int kPinTx = 19; // PLC-TX -> Q5 (must stay LOW while we only listen)
constexpr int kPinRx = 20; // PLC-RX <- Q3
constexpr uint32_t kBaud =2400;

// How long past the end of our own TX to keep treating ring bytes as our
// own echo rather than real incoming data. Picked from the DIAG-probe
// capture earlier this session, where the one glitchy post-TX burst showed
// somewhat-elevated but still sub-15ms inter-byte gaps before traffic went
// back to normal - this gives a bit of margin past that for the electrical
// transient to settle, without blanket-clearing everything the way the old
// ringClear()+uart_flush_input() combo did.
constexpr int64_t kEchoMarginUs = 20 * 1000;

// Timestamped byte ring, filled by s_uartEventTask as bytes actually land
// (not when plc_protocol::poll() next happens to call readTimedByte) -
// single producer (the event task), single consumer (poll()), so plain
// indices without a lock are fine.
struct TimedByte {
  uint8_t value;
  int64_t tsUs;
};
constexpr size_t kRingSize = 512;
TimedByte s_ring[kRingSize];
volatile size_t s_ringHead = 0; // next slot the event task writes
volatile size_t s_ringTail = 0; // next slot readTimedByte() consumes

QueueHandle_t s_uartQueue = nullptr;

// Diagnostic only, protocol-provider-requested: counts bytes at the
// lowest level we have access to (right where uart_read_bytes() pulls them
// off the driver), separate from anything plc_protocol's burst logger does.
volatile uint32_t s_uartBytesRead = 0;

void ringPush(uint8_t b, int64_t ts) {
  size_t next = (s_ringHead + 1) % kRingSize;
  if (next == s_ringTail) return; // full - drop rather than block
  s_ring[s_ringHead] = {b, ts};
  s_ringHead = next;
}

// Drops only ring entries timestamped at or before cutoffUs, stopping at
// the first entry after it - that entry and everything behind it (real
// data, not our echo) is left for readTimedByte() to consume normally.
// Confirmed via the uart_fifo_bytes/burst_logged byte counters that the
// old blanket ringClear() was destroying roughly a whole burst's worth of
// real bytes per reply, not just echo - this targets exactly the TX+decay
// window instead. Both this and readTimedByte() only ever run from the
// app task (sendBytes() is called from replyB5(), same task as poll()), so
// there's no new race on s_ringTail beyond what already existed.
size_t dropRingThrough(int64_t cutoffUs, int64_t txEndUs, const uint8_t *expect,
                       size_t expectN) {
  uint8_t got[32];
  size_t dropped = 0;
  char line[256];
  int pos = snprintf(line, sizeof(line), "[PLC] TX echo:");
  while (s_ringTail != s_ringHead && s_ring[s_ringTail].tsUs <= cutoffUs) {
    uint8_t v = s_ring[s_ringTail].value;
    int64_t relUs = s_ring[s_ringTail].tsUs - txEndUs;
    if (dropped < sizeof(got)) got[dropped] = v;
    if (pos < (int)sizeof(line) - 20) {
      pos += snprintf(line + pos, sizeof(line) - pos, " %02X(%+lldus)", v, (long long)relUs);
    }
    s_ringTail = (s_ringTail + 1) % kRingSize;
    dropped++;
  }
  if (dropped == 0) {
    printf("[PLC] TX echo: none\n");
    return 0;
  }
  printf("%s\n", line);

  bool match = false;
  if (expect != nullptr && expectN == 6 && dropped >= 6) {
    size_t nchk = dropped > sizeof(got) ? sizeof(got) : dropped;
    for (size_t i = 0; i + 6 <= nchk; i++) {
      bool ok = true;
      for (size_t j = 0; j < 6; j++) {
        if (got[i + j] != expect[j]) {
          ok = false;
          break;
        }
      }
      if (ok) {
        match = true;
        break;
      }
    }
  }
  printf("[PLC] ECHO %s\n", match ? "OK" : "MISMATCH");
  return dropped;
}

// Blocks on the driver's event queue and timestamps bytes as the ISR
// reports them, with rx_full_threshold/rx_timeout set to 1 symbol so an
// event fires essentially per byte - this is what makes the timestamp
// meaningful as real arrival time instead of drain-loop overhead.
void uartEventTask(void *arg) {
  (void)arg;
  uart_event_t event;
  uint8_t tmp[64];
  for (;;) {
    if (xQueueReceive(s_uartQueue, &event, portMAX_DELAY) != pdTRUE) continue;
    switch (event.type) {
      case UART_DATA: {
        int64_t ts = esp_timer_get_time();
        size_t want = event.size;
        while (want > 0) {
          size_t chunk = want > sizeof(tmp) ? sizeof(tmp) : want;
          int got = uart_read_bytes(UART_NUM_2, tmp, chunk, 0);
          if (got <= 0) break;
          s_uartBytesRead += (uint32_t)got;
          for (int i = 0; i < got; i++) ringPush((uint8_t)(tmp[i] ^ 0xFF), ts);
          want -= (size_t)got;
        }
        break;
      }
      case UART_FIFO_OVF:
      case UART_BUFFER_FULL:
        uart_flush_input(UART_NUM_2);
        xQueueReset(s_uartQueue);
        break;
      default:
        break;
    }
  }
}

void disableUsbPads() {
  // ROM / USB-Serial-JTAG owns GPIO19/20 (D-/D+). Same net as Q5.
  SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_PAD_ENABLE_OVERRIDE);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_WRAP_OTG_CONF_REG, USB_WRAP_USB_PAD_ENABLE);
}

// Do not use gpio_reset_pin(19): it enables a pull-up, Q5 turns on, LCD dies.
void driveTxLow() {
  disableUsbPads();
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPinTx;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  gpio_set_level((gpio_num_t)kPinTx, 0);
}

} // namespace

void holdQ5Off() { driveTxLow(); }

namespace {
struct EarlyQ5Off {
  EarlyQ5Off() { holdQ5Off(); }
} s_earlyQ5Off;
} // namespace

void init() {
  holdQ5Off();
  disableUsbPads();
  // USB-Serial-JTAG reserved these at boot. RX must stay UART2's, always.
  esp_gpio_revoke(1ULL << kPinRx);
  esp_gpio_revoke(1ULL << kPinTx);

  uart_config_t cfg = {};
  cfg.baud_rate = kBaud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_driver_install(UART_NUM_2, 1024, 256, 40, &s_uartQueue, 0);
  uart_param_config(UART_NUM_2, &cfg);
  // RX always bound. TX left unbound so GPIO19 stays LOW (Q5 off).
  uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, kPinRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  // Idle is LOW, start bit HIGH on this analog line. Invert RX so the UART
  // finds byte starts. Data bits are still complemented; ringPush XORs 0xFF.
  uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_RXD_INV);
  uart_set_rx_full_threshold(UART_NUM_2, 1);
  uart_set_rx_timeout(UART_NUM_2, 1);
  xTaskCreate(uartEventTask, "plc_uart_evt", 3072, nullptr, configMAX_PRIORITIES - 3, nullptr);

  printf("[PLC] RX pin idle state: %d (physical; 0 = inverted idle LOW)\n",
         gpio_get_level((gpio_num_t)kPinRx));
  printf("[PLC] RX UART2 2400 GPIO%d RXD_INV + XOR 0xFF\n", kPinRx);
}

bool sendBytes(const uint8_t *data, size_t n) {
  disableUsbPads();
  esp_gpio_revoke(1ULL << kPinTx);
  gpio_set_level((gpio_num_t)kPinTx, 0);

  esp_err_t pin = uart_set_pin(UART_NUM_2, kPinTx, kPinRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  // Q5 is on when GPIO19 is HIGH. UART mark/idle is HIGH, so without invert
  // the whole TX window holds Q5 on and PLC 5V / LCD die. Invert: idle LOW
  // (Q5 off), data pulses HIGH.
  uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
  uint8_t wire[64];
  if (n > sizeof(wire)) return false;
  for (size_t i = 0; i < n; i++) wire[i] = (uint8_t)(data[i] ^ 0xFF);
  int wrote = uart_write_bytes(UART_NUM_2, wire, n);
  esp_err_t done = uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(100));
  int64_t txEndUs = esp_timer_get_time();

  uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_RXD_INV);
  uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, kPinRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  driveTxLow();
  vTaskDelay(pdMS_TO_TICKS(25));
  // Confirmed this session: Q5's own switching transient bleeds into Q3's
  // RX sense circuit and gets timestamped/queued like real incoming data -
  // decodes consistently enough to look like a genuine fast-polling MAIN
  // reply and self-sustain a reply->echo->reply loop with no MAIN involved
  // at all. The old fix (uart_flush_input() + ringClear(), blanket) also
  // confirmed-destroyed real bytes that happened to land during/right
  // after our TX (~1 burst's worth per reply, via the uart_fifo_bytes vs
  // burst_logged counters) - this drops only the ring entries actually
  // timestamped within the TX+decay window, leaving anything after intact
  // for poll() to read normally. No uart_flush_input() needed: the
  // timestamp already tells us what's echo vs. real, so there's nothing to
  // gain from also nuking the driver's hardware FIFO.
  dropRingThrough(txEndUs + kEchoMarginUs, txEndUs, data, n);
  return wrote == (int)n && done == ESP_OK && pin == ESP_OK;
}

int rxPinLevel() { return gpio_get_level((gpio_num_t)kPinRx); }

uint32_t uartBytesReadCount() { return s_uartBytesRead; }

bool readTimedByte(uint8_t *out, int64_t *tsUs) {
  if (s_ringTail == s_ringHead) return false;
  *out = s_ring[s_ringTail].value;
  *tsUs = s_ring[s_ringTail].tsUs;
  s_ringTail = (s_ringTail + 1) % kRingSize;
  return true;
}

} // namespace plc_link
