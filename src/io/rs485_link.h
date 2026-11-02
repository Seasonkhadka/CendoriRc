/*
 * RS-485 slave link to the monitoring PC (external protocol, we are MAIN).
 *
 * UART1, 9600 8N1, GPIO17 TX / GPIO18 RX / GPIO48 DE+RE (U10 THVD1400),
 * see rs485/Rs485Config.h. A task listens for 10-byte request frames,
 * hands valid ones to Rs485Controller and transmits the reply. DE is HIGH
 * only while our reply is on the wire. Replaces the old ahu_link master
 * code on the same pins.
 */
#pragma once

namespace rs485_link {

void init();

} // namespace rs485_link
