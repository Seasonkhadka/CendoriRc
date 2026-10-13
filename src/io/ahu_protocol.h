/*
 * Ventilation controller (AHU) command protocol, layered on ahu_link's
 * RS-485 transport. Fixed 10-byte frames:
 *   [0] header  [1] group  [2] id  [3..8] D0..D5 (payload)  [9] checksum
 *   checksum = XOR of bytes 0..8
 *
 * Source: manufacturer doc "ERV 외부 통신 프로토콜 (SLBLDM-V1000DWMDCLH)",
 * S.L Technologies, doc# SLT-ERV_Protc0002 Ver 1.2 (2026.06.09). RS-485,
 * 9600 8N1, half-duplex, >=100ms recommended between transactions.
 *
 * Group: 0x00-0xFE (the requester's/central-controller's id - ours).
 * ID: real id is 0x62. Earlier testing assumed 0x64 was confirmed because
 * replies echoed id 0x64 back - that was wrong reasoning: every reply's
 * id field just mirrors whatever id was in the matching request (send
 * 0x64, get 0x64 back; send 0x62, get 0x62 back), so an echoed id is NOT
 * proof of the unit's real address on its own. An id=0x01 experiment
 * based on the doc's stated 0x01-0x20 "typical" range was also wrong -
 * don't use that range to override a known-correct real id either.
 * Reference checksums below (doc's own worked examples, using the doc's
 * stated id=1, NOT this unit's real id=0x64) are still useful for
 * validating the XOR algorithm itself, hand-verified:
 *   status req  (group 0, id 1):             D5 00 01 00 00 00 00 00 00 D4
 *   status resp (example - low fan running):  B5 00 01 11 32 1E 03 03 00 89
 *   ctrl req  (group 0, id 1, medium fan on): E5 00 01 12 FE FE FE FE 00 F6
 *   ctrl resp (applied immediately - no       B5 00 01 12 32 1E 03 03 00 8A
 *              separate confirm needed per spec):
 *   ctrl req  (ventilation off):              E5 00 01 00 FE FE FE FE 00 F4*
 *   ctrl resp (off, applied):                 B5 00 01 00 32 1E 03 03 00 98
 *   (* the doc's own stated checksum for this one line doesn't match
 *   XOR-of-bytes-0..8 the way every other example here does - 0xE4 is
 *   what the documented algorithm actually produces, so treat 0xF4 as a
 *   likely doc typo, not a different checksum rule.)
 * Per spec the B5 reply to an E5 should already reflect the new state -
 * ahu_protocol.cpp's power_on() checks the direct E5 reply first for
 * that reason, falling back to a D5 re-poll loop only if it doesn't look
 * applied yet. The real open question is still why id=0x64 writes don't
 * take effect even though id=0x64 reads work fine - NOT an id problem,
 * per the above.
 *
 * ---- D5 (status req) / E5 (control req) -> B5 (resp) ----
 * D0 (MODE & fan speed) - 0x00 on status req, 0xFE = no change on ctrl:
 *   bit7       bypass (0=off 1=on)
 *   bit6:4     mode   (0=power off, 1=ventilation, 2=reservation-run,
 *                       4=element-timer reset, 5=filter-timer reset)
 *   bit3       auto   (0=manual 1=auto)
 *   bit2       lock   (0=unlocked 1=locked)
 *   bit1:0     fan speed (0=stop 1=low 2=mid 3=high)
 *   NOTE: mode value 3 (this code's kStopReserve=0x30) is NOT in the
 *   documented mode enum above (only 0,1,2,4,5 are defined) - that's an
 *   unconfirmed/reverse-engineered extension, not from this spec.
 * D1 element-timer set (0xFE=no change, 0x63=stop) / resp: timer hours
 * D2 filter-timer set  (0xFE=no change, 0x63=stop) / resp: timer hours
 * D3 reservation-start hour set (0xFE=no change) / resp: hour
 * D4 reservation-stop  hour set (0xFE=no change) / resp: hour
 * D5 unused on request; on response, error bits:
 *   bit4 "no equipment" (central-control linkage only), bit3 main-comm
 *   error, bit1 FAN2(supply) fault, bit0 FAN1(exhaust) fault - 0=ok,1=bad
 *
 * ---- D7 (status req) / E7 (control req) -> B7 (resp): room controller ----
 * D3 (settings, 0x00 on status req):
 *   bit7 heater enable, bit4 clean mode, bit3 exhaust-only mode,
 *   bit2 supply-only mode, bit1 sleep mode, bit0 CO2 mode (all 0=off 1=on)
 * D4 bath/hood interlock (read-only, bit7/bit6) - 0x00 on any request
 * resp-only: D1 room temp (raw-20, range -20..60C), D2 room RH% (0-100),
 *   D5 error: bit7 filter-replace alarm, bit6 filter-clean alarm,
 *   bit5 unit-stopped-by-filter-alarm, bit4 fire alarm
 *
 * ---- C5 (control req) / C6 (status req) -> A5 (resp): VSP (fan voltage) ----
 * D0/D1/D2 = Fan1(supply) low/mid/high VSP, D3/D4/D5 = Fan2(exhaust)
 * low/mid/high VSP. Each byte: high nibble = whole volts, low nibble =
 * tenths (e.g. 2.3V -> 0x17, 4.5V -> 0x2D).
 *
 * ---- Read-only sensor/telemetry frames (not implemented here yet, but
 * documented so the header bytes are never reused for anything else) ----
 *   D6 -> B6: room sensor 1       - D0 PM10, D1/D2 CO2 (hundreds/ones),
 *             D3 PM2.5, D4/D5 TVOC (hi/lo byte)
 *   D8 -> B8: integrated sensor 1 - D0 PM2.5, D1 PM10, D2/D3 CO2
 *             (hundreds/ones), D4 outdoor temp (raw-20), D5 outdoor RH%
 *   DB -> BB: integrated sensor 2 - D0 CO (ppm), D1/D2 TVOC (hi/lo),
 *             D3 PM1.0, D4/D5 differential pressure (hi/lo, 0-1000 Pa)
 *   DC -> BC: room controller CO sensor - D1 CO ppm (SHR-L2 room unit)
 *   D9 -> B9: fan RPM - D0/D1 SA(supply) fan hi/lo (x100/x1 rpm),
 *             D2/D3 EA(exhaust) fan hi/lo, D4/D5 OA(outdoor) fan hi/lo
 *
 * Unused E5 fields are 0xFE except reservation, which writes one hour
 * and leaves the other as 0xFE.
 */
#pragma once

#include <Arduino.h>

namespace ahu_protocol {

void init();

bool power_on();
bool power_off();
bool set_ventilation();
bool set_bypass();
bool set_auto();
bool set_volume(uint8_t vol); // 0=off 1=low 2=mid 3=high
bool start_reservation(uint8_t start_hour, uint8_t stop_hour);
bool stop_reservation(uint8_t stop_hour);
bool set_clean(bool on);
bool set_fan_voltage(uint8_t fan1_low, uint8_t fan1_mid, uint8_t fan1_high,
                      uint8_t fan2_low, uint8_t fan2_mid, uint8_t fan2_high);

uint8_t shadowMode(); // last D5/B5 mode byte - for debugging/UI only

} // namespace ahu_protocol
