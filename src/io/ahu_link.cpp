#include "ahu_link.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>

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

void logHex(const char *tag, const uint8_t *buf, size_t n) {
  Serial.printf("[AHU] %s (%u):", tag, (unsigned)n);
  for (size_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  Serial.println();
}

} // namespace

void setTe(bool drive) {
  gpio_set_level((gpio_num_t)kPinTe, drive ? 1 : 0);
}

void init() {
  gpio_reset_pin((gpio_num_t)kPinTe);
  gpio_set_direction((gpio_num_t)kPinTe, GPIO_MODE_OUTPUT);
  setTe(false);

  Serial1.setRxBufferSize(256);
  Serial1.begin(kBaud, SERIAL_8N1, kPinRx, kPinTx);
  // Re-bind after RGB LCD init in case the GPIO matrix dropped UART1.
  uart_set_pin(UART_NUM_1, kPinTx, kPinRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  Serial.printf("[AHU] UART1 9600 8N1 TX=GPIO%d RX=GPIO%d TE=GPIO%d (idle=%d)\n",
                kPinTx, kPinRx, kPinTe, gpio_get_level((gpio_num_t)kPinTe));
  Serial.println("[AHU] COM4 is ESP-PROG UART0 @ 115200 - not the RS-485 bus");

  // Internal UART loopback: never leaves the chip. PASS = Serial1 is
  // really UART1 on 17/18. FAIL = pin/UART mapping is wrong in firmware.
  while (Serial1.available()) Serial1.read();
  uart_set_loop_back(UART_NUM_1, true);
  Serial1.write(0xA5);
  Serial1.flush();
  uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(50));
  delay(10);
  int loopGot = Serial1.available() ? Serial1.read() : -1;
  uart_set_loop_back(UART_NUM_1, false);
  while (Serial1.available()) Serial1.read();
  if (loopGot == 0xA5) Serial.println("[AHU] UART1 loopback PASS");
  else Serial.printf("[AHU] UART1 loopback FAIL (got=%d)\n", loopGot);

  setTe(true);
  Serial.printf("[AHU] TE drive=%d\n", gpio_get_level((gpio_num_t)kPinTe));
  setTe(false);
  Serial.printf("[AHU] TE listen=%d\n", gpio_get_level((gpio_num_t)kPinTe));
}

size_t transact(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax, uint32_t timeoutMs) {
  logHex("TX", tx, txLen);

  for (int attempt = 0; attempt < 2; attempt++) {
    while (Serial1.available()) Serial1.read();

    setTe(true);
    delayMicroseconds(100);
    Serial1.write(tx, txLen);
    Serial1.flush();

    if (uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(kTxDoneTimeoutMs)) != ESP_OK) {
      Serial.println("[AHU] uart_wait_tx_done timed out - check GPIO48/U10 wiring");
    }
    delayMicroseconds(kTeHoldAfterTxUs);
    setTe(false);
    delayMicroseconds(kTeReleaseSettleUs);

    size_t got = 0;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && got < rxMax) {
      if (Serial1.available()) {
        rx[got++] = (uint8_t)Serial1.read();
      }
    }
    if (got > 0) {
      logHex("RX", rx, got);
      return got;
    }
    Serial.printf("[AHU] RX empty (attempt %d)\n", attempt + 1);
  }
  return 0;
}

} // namespace ahu_link
