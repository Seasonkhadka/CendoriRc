#include "ahu_link.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>

#include "port/esp_time.h"

namespace ahu_link {

namespace {

// Schematic U7: IO17=485-TX, IO18=485-RX, IO48=485-TE.
constexpr int kPinTx = 17;
constexpr int kPinRx = 18;
constexpr int kPinTe = 48; // DE/RE on U10 - HIGH = drive the bus, LOW = listen
constexpr uint32_t kBaud = 9600;
constexpr uint32_t kTxDoneTimeoutMs = 50;
constexpr uint32_t kTeHoldAfterTxUs = 2000;  // last stop-bit + transceiver turnaround
constexpr uint32_t kTeReleaseSettleUs = 1000;
constexpr int kRxBufSize = 256;

} // namespace

void setTe(bool drive) {
  gpio_set_level((gpio_num_t)kPinTe, drive ? 1 : 0);
}

void init() {
  gpio_reset_pin((gpio_num_t)kPinTe);
  gpio_set_direction((gpio_num_t)kPinTe, GPIO_MODE_OUTPUT);
  setTe(false);

  uart_config_t cfg = {};
  cfg.baud_rate = kBaud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_driver_install(UART_NUM_1, kRxBufSize * 2, 0, 0, NULL, 0);
  uart_param_config(UART_NUM_1, &cfg);
  // Re-bind after RGB LCD init in case the GPIO matrix dropped UART1.
  uart_set_pin(UART_NUM_1, kPinTx, kPinRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  uart_flush_input(UART_NUM_1);
}

size_t transact(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax, uint32_t timeoutMs) {
  for (int attempt = 0; attempt < 2; attempt++) {
    uart_flush_input(UART_NUM_1);

    setTe(true);
    delayMicroseconds(100);
    uart_write_bytes(UART_NUM_1, (const char *)tx, txLen);

    (void)uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(kTxDoneTimeoutMs));
    delayMicroseconds(kTeHoldAfterTxUs);
    setTe(false);
    delayMicroseconds(kTeReleaseSettleUs);

    int got = uart_read_bytes(UART_NUM_1, rx, rxMax, pdMS_TO_TICKS(timeoutMs));
    if (got > 0) return (size_t)got;
  }
  return 0;
}

} // namespace ahu_link
