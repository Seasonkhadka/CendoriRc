/*
 * RS-485 "external protocol" - control layer removed. Confirmed on real
 * hardware (repeated TX, always "RX empty"/no reply) that this bus does
 * not reach the real MainControl; the working control path is PLC (see
 * plc_protocol.h). Kept only as a thin init() so main.cpp's existing
 * passive-listen/D5-poll diagnostics (ahu_link::transact() calls directly)
 * still have UART1 brought up - those are read-only bus sniffing, not
 * control, and still useful for confirming this bus's actual traffic (or
 * lack of it) if that's ever revisited.
 *
 * Frame format/checksum notes and the full command field layout this used
 * to implement are preserved in git history (see the espidf-migration
 * branch prior to this file's gutting) rather than repeated here.
 */
#pragma once

namespace ahu_protocol {

void init();

} // namespace ahu_protocol
