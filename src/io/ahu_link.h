/*
 * RS-485 transport to the ventilation (AHU) controller PCB, over UART1.
 * Nothing in this codebase used UART1/RS-485 before this file - built
 * from scratch against the schematic's traced pins:
 *
 *   TX (485-TX)        = GPIO17  -> U10 D
 *   RX (485-RX)        = GPIO18  -> U10 R
 *   Direction (485-TE) = GPIO48  -> U10 DE+/RE (tied)
 *   9600 8N1
 *
 * Pin map from CD-WD-RMC-V1.0-SCH-260811: U7 IO17=485-TX, IO18=485-RX,
 * IO48=485-TE; U10 THVD1400 D/R/DE+/RE. A later swap to 18/17 drove
 * the transceiver's R pin instead of D, so UART TX never reached A/B.
 *
 * Half-duplex: the bus is a shared pair, so TE must sit LOW (listening)
 * except for the brief window where we're actively pushing bytes out.
 */
#pragma once

#include <Arduino.h>

namespace ahu_link {

// Default response window for a transact() call that doesn't pass its own
// timeoutMs. Status reads (D5) tend to answer quickly; a write (E5) may take
// the real controller noticeably longer to process and ack, so callers that
// care should pass a longer timeoutMs explicitly rather than rely on this.
constexpr uint32_t kDefaultResponseTimeoutMs = 500; // matches CendoriAppDC usbRead_(500)

void init();

// Sends txLen bytes, then listens for a response into rx (up to rxMax
// bytes) until either rxMax bytes arrive or the response window
// (timeoutMs) times out with nothing new. Retries once if the first
// attempt comes back completely empty. Returns the number of bytes
// actually captured - the caller (ahu_protocol) knows the expected frame
// length/header, so frame validation happens there, not here.
size_t transact(const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxMax,
                 uint32_t timeoutMs = kDefaultResponseTimeoutMs);

} // namespace ahu_link
