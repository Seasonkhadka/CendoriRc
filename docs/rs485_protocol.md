# RS-485 External Protocol (PC ↔ CendoriRC as MAIN)

Source: `ERV 외부 통신 프로토콜(센도리)_2606091.pdf`, Ver 1.2 (2026.06.09).
The CendoriRC board answers the monitoring PC **as the MainControl** (③ in the
document). The PC polls; the board only answers. The PLC link to the real main
unit (④) is unchanged: changes made from the PC go to the main unit in the next
PLC reply, exactly like changes made on the RC screen.

## 1. Link

| Item | Value |
|---|---|
| Hardware | UART1, GPIO17 TX, GPIO18 RX, GPIO48 DE/RE (U10 THVD1400) |
| Format | RS-485 half duplex, 9600 bps, 8N1 |
| Frame | 10 bytes: `HEADER, Group, ID, D0..D5, CHECKSUM` (XOR of bytes 0–8) |
| Address | Group = `RS485_GROUP` (0x00). ID = the RC's **기기 설정 → ID 설정** value (the PC supports 1–32) |
| Reply | ~5 ms after the request (`RS485_REPLY_DELAY_MS`), only for our Group + ID, with the state **after** applying a control request |
| Code | `src/io/rs485_link.*` (UART task), `src/rs485/` (Config, Struct, Service, Controller) |

## 2. Frames

| PC | Reply | Content | Source in the RC |
|---|---|---|---|
| D5 / E5 | B5 | D0 mode & fan, D1 element time (0), D2 filter time, D3/D4 start/stop reservation (h), D5 error | `RcDto` |
| D6 | B6 | PM10, CO2 ÷100 / %100, PM2.5, TVOC hi/lo | RC sensors |
| D7 / E7 | B7 | Indoor temp +20, humidity, functions, bath/hood link, alarms | `RcDto` + RC sensors |
| D8 | B8 | PM2.5, PM10 (main unit D8 over PLC, else 0), CO2 + outdoor temp (main unit D5), outdoor RH 0 | `RcDto` |
| DB | BB | CO (0 until calibrated), TVOC, PM1.0, differential pressure 0 | RC sensors |
| DC | BC | RC CO (0 until calibrated) | RC sensors |
| D9 | B9 | Fan RPM - all 0 (not available) | - |
| C5 / C6 | A5 | VSP Fan1 (supply) L/M/H, Fan2 (exhaust) L/M/H, heat-recovery set | `RcDto` |

### Control requests (`RS485_ALLOW_CONTROL` = 1)

They go through `RcController`, so the RC screen rules apply (LOCK, power off,
auto, sleep).

- **E5 D0** (0xFE = no change): unlock first; mode 0 = power off, 1 = run
  (+ AUTO / BYPASS bits select the mode column; neither = heat recovery;
  clean / AI are kept), fan 1–3; 2 / 3 = start / stop reservation using D3 / D4
  hours (1–24); 5 = filter time reset (same as 필터 초기화 on the RC); 4 =
  element reset (ignored); lock last.
- **E5 D2** filter time set: ignored while `RC_FILTER_INIT_FIXED` (2,000 h);
  otherwise applied when it differs from the current value.
- **E7 D3**: heater, clean, exhaust, supply, sleep - only changed bits are
  applied. D4 (links) can't be controlled.
- **C5**: six values, all must be 0.6–5.4 V, otherwise the whole request is
  ignored.

### Values

- **Filter time (B5 D2)** = the remaining time shown on the RC 필터 상태 page,
  in 100-hour units, rounded up (2,000 h → 20, 50 h → 1, 0 when used up).
- **VSP** = volts × 10 (2.3 V → 23), following the document's examples. The RC
  works in 0.05 V steps, so a reading is rounded to 0.1 V (2.45 V → 25).
- **Reservation** hours are rounded up from the RC's 10-minute steps (1:30 → 2).
- **B5 D5 errors**: bit1 FAN2 = main supply-fan error, bit0 FAN1 = main
  exhaust-fan error, bit4 = PLC link to the main unit lost.
- **B7 D5 alarms**: filter replace (main alarm bit or the RC's own 2,000 h
  count), filter clean, stopped by filter alarm, fire.
- **TVOC** is µg/m³, converted from the SGP40 VOC index. **CO** is ppm from the ME2-CO cell.

## 3. Points to confirm with the PC side

1. The example `E5 00 01 00 FE FE FE FE 00 F4` has a wrong checksum - it is **E4**
   (all other examples check out).
2. VSP: the text says high nibble = volts, low nibble = 0.1 V, but the examples
   are volts × 10. We follow the examples (`RS485_VSP_X10` = 1).
3. FAN1/FAN2: VSP says Motor1 = supply, Motor2 = exhaust; the B5 error byte says
   FAN1 = exhaust, FAN2 = supply. We follow each section as written.
4. D0 mode 3 (stop reservation) is not listed; we use 3 as on the PLC side.
5. Filter / element time unit: the example says "50/30 시간"; we send the RC
   value in 100-hour units. Element time is 0.
6. ID range 1–32 vs the RC's 1–99 (the ID page shows a hint).
7. Reply delay is not specified (we use 5 ms).

Host test (`.pio/hosttest/t485.cpp`, not part of the build): document example
checksums, VSP / reservation conversion and B5, B6, B7, A5, BB, B9 replies -
all pass.
