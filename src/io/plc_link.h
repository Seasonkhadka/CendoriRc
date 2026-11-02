/*
 * PLC (power-line communication) transport to the ERV MainControl, over
 * UART2. This is the "internal protocol" (메인 ↔ R/C) path - separate from
 * ahu_link's RS-485 "external protocol" path, and NOT interchangeable
 * with it. Per schematic trace (CD-WD-RMC-V1.0-SCH-260811, sheet 1, U7
 * pins), confirmed directly off a cropped screenshot:
 *   PLC-TX = GPIO19 -> Q5, switches the rectified PLC+/PLC- power node
 *   PLC-RX = GPIO20 <- Q3 senses that node through a 150k divider (R46)
 *   2400 baud, 8N1, no parity (per the internal protocol doc)
 * No DE/RE-style direction pin exists for this path (unlike 485-TE) - the
 * TX/RX transistor circuits are separate from each other and from U10,
 * not a shared half-duplex bus driver.
 *
 * Per the internal protocol doc (대외비_ERV 내부 프로토콜 V3.2), MainControl
 * is the master on this link: it polls, and the Room Controller (this
 * board) is the slave that listens and replies. That is the opposite
 * role from ahu_link/ahu_protocol, which act as master against
 * MainControl's separate RS-485 port.
 *
 * RX is always on GPIO20. GPIO19 (Q5) stays a GPIO LOW except for the
 * short window in sendBytes() - idle-high UART TX on 19 would load PLC+
 * and blank the LCD.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace plc_link {

// Call before RGB/backlight: ESP32-S3 USB-Serial-JTAG owns GPIO19/20
// from reset, and GPIO19 HIGH turns Q5 on, which kills PLC-derived 5V.
void holdQ5Off();

void init();

// Non-blocking: pulls one raw byte off UART2 if available, along with the
// esp_timer_get_time() timestamp of when a dedicated UART event task saw it
// arrive (not when this function happened to be called) - so gaps computed
// from tsUs reflect real wire timing even if the caller's poll loop runs
// much slower than the baud rate and bytes queue up between calls.
// plc_protocol.cpp owns frame sync/parsing.
bool readTimedByte(uint8_t *out, int64_t *tsUs);

// Bind GPIO19 to UART2 TX, write, wait, then GPIO19 LOW again.
// Only call from a D5/E5 reply - not idle.
bool sendBytes(const uint8_t *data, size_t n);

// Raw GPIO20 level, independent of whatever UART2 last decoded - for the
// protocol provider's RX idle-state check (1 = HIGH = standard non-inverted
// UART idle). init() logs this once right away; callers can sample it again
// later once the bus has had time to settle.
int rxPinLevel();

// Cumulative count of bytes actually pulled off the UART2 driver via
// uart_read_bytes() inside uartEventTask - the lowest-level point in this
// codebase where a byte is confirmed to have reached the peripheral/driver,
// independent of the ring buffer or plc_protocol's burst grouping. Compare
// against plc_protocol's own cumulative "bytes appended to a burst" count
// to tell apart "the byte never reached the UART peripheral" (hardware/line
// issue) from "our own pipeline dropped or misplaced a byte that did
// arrive" (our bug).
uint32_t uartBytesReadCount();

} // namespace plc_link
