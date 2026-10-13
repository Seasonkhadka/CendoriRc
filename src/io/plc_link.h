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
 * Listen only: UART2 RX on GPIO20. GPIO19 (Q5) is held LOW so it does
 * not load PLC+ / 5V. Do not attach UART TX to GPIO19 while powered
 * from J1 — idle-high TX turns Q5 on and the LCD goes blank.
 */
#pragma once

#include <Arduino.h>

namespace plc_link {

// Call before RGB/backlight: ESP32-S3 USB-Serial-JTAG owns GPIO19/20
// from reset, and GPIO19 HIGH turns Q5 on, which kills PLC-derived 5V.
void holdQ5Off();

void init();

// Non-blocking: call every loop() iteration. Drains whatever bytes are
// currently available on UART2 and logs them (hex + a relative
// timestamp), starting a new log line whenever there's a gap since the
// last byte, so real traffic bursts/timing are visible without assuming
// a frame length up front.
void poll();

// True once a byte has been seen on PLC-RX within the last kLinkTimeoutMs
// (see .cpp). This is just "is something toggling the PLC-RX line right
// now" - not a real frame/protocol check yet - so it is a presence
// heuristic, not proof MainControl is polling us correctly.
bool isConnected();

} // namespace plc_link
