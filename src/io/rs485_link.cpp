#include "rs485_link.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rs485/Rs485Config.h"
#include "rs485/Rs485Controller.h"
#include "rs485/Rs485Service.h"

namespace rs485_link {

namespace {

const char *TAG = "RS485";
constexpr uart_port_t kUart = (uart_port_t)RS485_UART_NUM;
constexpr int kRxBuf = 256;
int64_t s_lastBusMs = 0;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

void markBus() { s_lastBusMs = nowMs(); }

void transmit(const RS485::Frame &f) {
  gpio_set_level((gpio_num_t)RS485_PIN_DE, 1);
  uart_write_bytes(kUart, (const char *)&f, RS485::FRAME_LEN);
  uart_wait_tx_done(kUart, pdMS_TO_TICKS(50));
  gpio_set_level((gpio_num_t)RS485_PIN_DE, 0);
  markBus();
}

void maybeAutoB6() {
  if (RS485_AUTO_B6_MS <= 0) return;
  if (nowMs() - s_lastBusMs < RS485_AUTO_B6_MS) return;
  RS485::Frame f;
  if (!Rs485Controller::instance().buildAutoB6(f)) return;
  transmit(f);
  if (RS485_LOG_FRAMES) {
    char hex[40];
    Rs485Service::toHex(f, hex, sizeof(hex));
    ESP_LOGI(TAG, "B6 TX auto: %s", hex);
  }
}

void handleFrame(const RS485::Frame &req) {
  char hex[40];
  markBus();
  if (!Rs485Service::checkChecksum(req)) {
    Rs485Service::toHex(req, hex, sizeof(hex));
    ESP_LOGW(TAG, "bad checksum: %s", hex);
    return;
  }
  RS485::Frame reply;
  if (!Rs485Controller::instance().handleRequest(req, reply)) return; // not our address
  vTaskDelay(pdMS_TO_TICKS(RS485_REPLY_DELAY_MS));
  transmit(reply);
  if (RS485_LOG_FRAMES) {
    Rs485Service::toHex(req, hex, sizeof(hex));
    ESP_LOGI(TAG, "%02X RX: %s", req.header, hex);
    Rs485Service::toHex(reply, hex, sizeof(hex));
    ESP_LOGI(TAG, "%02X TX: %s", reply.header, hex);
  }
}

// Byte-wise frame assembly: a frame starts with a request header and must
// arrive without a gap of RS485_FRAME_GAP_MS; anything else is dropped.
void linkTask(void *) {
  uint8_t buf[RS485::FRAME_LEN];
  int len = 0;
  for (;;) {
    uint8_t b;
    const int n = uart_read_bytes(kUart, &b, 1, pdMS_TO_TICKS(RS485_FRAME_GAP_MS));
    if (n != 1) {
      len = 0; // gap: drop a partial frame
      maybeAutoB6();
      continue;
    }
    if (len == 0 && !Rs485Service::isRequestHeader(b)) continue; // resync on a header byte
    buf[len++] = b;
    if (len == RS485::FRAME_LEN) {
      RS485::Frame f;
      memcpy(&f, buf, sizeof(f));
      handleFrame(f);
      len = 0;
    }
  }
}

} // namespace

void init() {
  gpio_reset_pin((gpio_num_t)RS485_PIN_DE);
  gpio_set_direction((gpio_num_t)RS485_PIN_DE, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)RS485_PIN_DE, 0); // listen

  uart_config_t cfg = {};
  cfg.baud_rate = RS485_BAUD;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(kUart, kRxBuf, 0, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGE(TAG, "uart driver install failed");
    return;
  }
  uart_param_config(kUart, &cfg);
  uart_set_pin(kUart, RS485_PIN_TX, RS485_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_flush_input(kUart);

  markBus();
  xTaskCreatePinnedToCore(linkTask, "rs485", 4096, nullptr, 6, nullptr, 0);
  printf("[RS485] slave on UART%d %d 8N1 TX=%d RX=%d DE=%d, group 0x%02X, id = RC device ID, auto B6 %dms\n",
         RS485_UART_NUM, RS485_BAUD, RS485_PIN_TX, RS485_PIN_RX, RS485_PIN_DE, RS485_GROUP,
         RS485_AUTO_B6_MS);
}

} // namespace rs485_link
