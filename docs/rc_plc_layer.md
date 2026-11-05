# CendoriRC – RC State / PLC Protocol Layer

Documentation for the room-controller (RC) state layer that sits between the
UI and the PLC link to the ERV main unit (MAIN), plus the related UI and
sensor changes on the `espidf-migration` branch.

Status: builds cleanly (ESP-IDF, PlatformIO). Frame builders verified with a
host-side test. **Not yet verified on the line** after the last changes
(device ID, mode/fan independence, SCD40 address detection).

---

## 1. Overview

```
 ┌─────────────┐   set*()     ┌──────────────────────────┐   onPlcReplyDue()   ┌────────────┐
 │  UI (LVGL)  │ ───────────► │       RcController       │ ◄────────────────── │ plc_protocol│
 │ main.cpp    │              │  RcDto (single owner,    │ ──────────────────► │ (frame sync,│
 │ settings    │ ◄─────────── │  mutex), pending frames, │   6-byte reply      │  140 ms     │
 │ dashboards  │  uiRefresh() │  NVS, link status        │                     │  reply)     │
 └─────────────┘  (UI task)   └──────────────────────────┘ ◄────────────────── └────────────┘
                                         ▲                    onPlcFrame()          ▲   │
                                         │ setLocalSensors()                        │   ▼
                                  ┌─────────────┐                              ┌────────────┐
                                  │  sensors    │                              │ plc_link   │
                                  │ (SHT40,SGP40│                              │ UART2 2400 │
                                  │ SCD40,PM…)  │                              │ GPIO19/20  │
                                  └─────────────┘                              └────────────┘
```

Core rules:

* **MAIN always speaks first; the RC only answers.** A UI action never sends
  a frame. It changes the state, and the next reply to MAIN carries it.
* **One owner of the state.** `RcDto` lives inside `RcController`, guarded by
  a FreeRTOS mutex that is held only while copying or changing fields (never
  while drawing or doing I/O).
* **The UI shows exactly what the next B5 sends.** Widgets never keep their
  own copy of mode or fan speed; they are redrawn from `RcDto`.
* **Every unconfirmed protocol detail is a named `#define`** in
  `src/rc/RcConfig.h`.

---

## 2. Files

| File | Role |
|---|---|
| `src/io/PLCStruct.h` | Frame layouts and bit-mask enums for the internal protocol (existing; added `FRAME_LEN`, `DEVICE_ID`, `static_assert`s). |
| `src/rc/RcConfig.h` | All tunable / unconfirmed protocol switches (section 7). |
| `src/rc/RcDto.h/.cpp` | The RC state: user settings + values received from MAIN. `structConverter()` per received frame, `makeB5()`. |
| `src/rc/PlcService.h/.cpp` | Stateless frame builders: checksum, B5 variants, B6, B7, B8, C5, hex logging. |
| `src/rc/RcController.h/.cpp` | State owner: command methods for the UI, PLC hooks, reply priority, link status, NVS persistence, UI refresh. |
| `src/io/plc_protocol.cpp/.h` | Existing PLC framing layer; now forwards frames to the controller and asks it for each reply. Added `plcMsSinceLastFrame()`. |
| `src/main.cpp` | Wires the controller into boot, the power key, the dashboards and the sensor cache. |
| `src/ui/ui_settings_screen.cpp/.h` | Settings screen routed to the controller; `set_state()` refresh entry. |
| `src/ui/ui_main_screen*.cpp/.h` | Dashboards: new `set_mode_badge()` and `set_fan_level()`. |
| `src/io/sensors.cpp/.h` | SCD40 (CO2) address detection and failure logging. |

---

## 3. Protocol summary (internal PLC protocol)

All frames are **6 bytes**: `Header, Data1, Data2, Data3, Data4, Checksum`.
Checksum = two's complement of the sum of bytes 0–4, so all 6 bytes sum to
`0x00`. Line encoding (inversion / XOR) is handled by `plc_link`.

### 3.1 MAIN → RC (handled in `RcController::handleFrame`)

| Header | Meaning | Fields → RcDto | Reply |
|---|---|---|---|
| `D5` | Status request 1 | D1 outdoor temp + 20 → `outdoorTemp`; D2·100 + D3 → `mainCo2`; D4 → `errCode` | B5 (or a pending C5 / time / function frame) |
| `E5` | Control request | D1 (if not `FE`): mode, bypass, auto, lock, fan (fan 0 keeps current fan). D2/D3 (if not `FE`): 2 filter hours, 3 start reservation h, 4 stop reservation h, 10 function bits | B5 with the **new** state |
| `D6` | Status request 2 | D1 bit3 → `heaterRunning`; D2 → `filterRemain`; D3/D4 → `filterUsage` | B6 (or B8, config) |
| `D7` | Status request 3 | none (all 00) | B7 |
| `E7` | Control request 3 | D3 bits: heater, clean, exhaust, supply, sleep, auto | B7 |
| `D8` | Sensor values | temp (bit7 = negative), humidity, PM2.5, PM10 → `ext*` | none |
| `D9` | Sensor values | TVOC hi/lo, temp + 20, humidity → `extTvoc`, `extTemp2`, `extHumidity2` | none |
| `A6` | VSP control | mode 1/2/5/6, L/M/H × 20 → stored as tenths of a volt | none |

Only **E5 and E7 change settings**; everything else is display data.

### 3.2 RC → MAIN (built in `RcDto` / `PlcService`)

**B5** (reply to D5 / E5)

| Byte | Content |
|---|---|
| D1 | `deviceId` (user ID 1–99, value used as-is: 98 → `0x62`; default `0x00`) |
| D2 | mode: 0 stop, 1 run, 2 start reservation, 3 stop reservation; 6/7 reservation time frame; 8 function frame |
| D3 | bit7 bypass, bit6 heater, bit5 control flag, bit4 clean, bit3 auto, bit2 lock, bits1–0 fan |
| D4 | 0, or hours (D2 = 6/7), or function bits (D2 = 8: exhaust, supply, dust, sleep) |

Verified example: running, heat recovery, fan low → `B5 00 01 01 00 49`.

**B6** – CO2 ÷ 20 (0–250), PM2.5, TVOC hi, TVOC lo
**B7** – indoor temp + 20, PM10, bits (filter alarm, exhaust, supply, sleep), humidity 0–99
**B8** – CO hi, CO lo, 00, 00
**C5** – VSP change from the RC: mode 1 exhaust / 2 supply, L/M/H as volts × 20

Sensor replies use the RC's **own** sensors. A sensor with no sample yet is
sent as `0x00` (the protocol has no "no data" value).

---

## 4. Data flow

### 4.1 UI → state

1. A UI event calls an `RcController` command method (e.g. `setVolumeState("2")`).
2. The method validates the input, takes the mutex, changes **only the target
   field(s)** of `RcDto`, and calls `markLocalChange()` (control flag + NVS dirty).
3. `notifyUi()` posts a refresh request; the UI redraws on its next tick.
4. Nothing is transmitted. The change goes out in the next reply to MAIN.

While `ahuLock` is on, every command except unlock (`setFunctionState("lock", false)`)
is rejected and returns `false`.

### 4.2 MAIN → state → UI

1. `plc_protocol` finds a valid frame (known header + checksum) and calls
   `onPlcFrame(frame)` → `RcController::handleFrame()`.
2. The matching `RcDto::structConverter()` runs under the mutex.
3. If a setting changed (E5/E7/A6), NVS is marked dirty.
4. `notifyUi()` posts a refresh. The PLC path never touches widgets.

### 4.3 State → MAIN

1. 140 ms after a D5/E5/D6/D7/E7, `plc_protocol` calls
   `onPlcReplyDue(header, out)` → `RcController::buildReply()`.
2. The reply is built from the current state under the mutex (microseconds).
3. `plc_protocol` transmits it and logs `"<HDR> TX: …"`.

Reply priority after **D5**:

1. Pending **C5** (exhaust fan first, then supply) – one per changed fan
2. Pending **reservation time** B5 (D2 = 6/7, D4 = hours)
3. Pending **function** B5 (D2 = 8)
4. Normal **B5** (carries the control flag once after a local change)

After **E5**: always a normal B5 reporting the state *after* the E5 was applied.

### 4.4 Pressure modes (양압 / 음압, RC only)

MAIN has one shared fan speed, so the RC builds the supply / exhaust
difference itself (`src/rc/RcPressure.*`):

* Two air paths: 전열 (`UI_POSITIVE` / `UI_NEGATIVE`, bypass bit off) and
  바이패스 (`UI_BP_POSITIVE` / `UI_BP_NEGATIVE`, bypass bit on). Clean and auto
  bits are off. Fan level 1 = setpoint, 2 = CO2 boost
  (`RcDto::effectiveVolume()`).
* On entering the mode, changing its setpoints, or booting in it, the RC
  queues that path's two **C5** frames (전열 1/2 or 바이패스 3/4, exhaust then
  supply) with the pressure table: L = setpoint, M = H = boost
  (`RcDto::pressureVspTable()`).
* On leaving it (other mode, 급기/배기 단독, or an E5/E7 that sets clean / auto /
  single fan), or on switching path, the RC queues the old path's C5 frames
  with the normal table again.
* `m_vspSkew` (NVS flags `FB2_VSP_SKEWED` / `FB2_VSP_SKEWED_BP`) records which
  set on MAIN holds the pressure table. After a power loss in another mode,
  boot restores the normal table.
* While a set is skewed, A6 frames for that set are ignored
  (`RC_PRESS_IGNORE_A6_WHILE_SKEWED`), so MAIN echoing the pressure table
  cannot overwrite the RC's normal table. Engineer VSP edits to that set are
  stored, and sent when the mode is left.

---

## 5. Threading and timing

* All PLC parsing, replies, sensor polling and LVGL currently run in one
  application task (`appTask`), plus the UART event task in `plc_link`.
* The mutex makes the controller safe if these are later split into tasks.
* `notifyUi()` only sets an atomic flag. `uiTick()` (called every loop from
  the UI task) coalesces repeated requests into one `dispatchUi()` call,
  which copies the state and invokes the data callback.
* Known limit: `plc_protocol` blocks the app task for 140 ms before each
  reply (existing behaviour), which eats into the 200 ms UI-refresh target.

---

## 6. Class and function reference

### 6.1 `RcDto` (`src/rc/RcDto.h`)

**User settings (saved to NVS):** `mode`, `volume`, `bypass`, `heater`, `clean`,
`autoDrive`, `ahuLock`, `exhaust`, `supply`, `night`, `dust`, `aiMode`,
`deviceId`, `startTime`, `stopTime`, `vspExhaustL/M/H`, `vspSupplyL/M/H`
(VSP in tenths of a volt).

**Received from MAIN (display only):** `errCode`, `outdoorTemp`, `mainCo2`,
`heaterRunning`, `filterRemain`, `filterUsage`, `filterSetHours`, `fireAlarm`,
`heat`, `bathLink`, `hoodLink`, bypass-fan VSP (`vspBp*`), external sensors
(`extTemp`, `extHumidity`, `extPm25`, `extPm10`, `extTvoc`, `extTemp2`,
`extHumidity2`).

**Link:** `rxMask` (which frames were ever received), `linkLost`.

| Function | Description |
|---|---|
| `RcDto()` / copy ctor / `operator=` | Construct with defaults; copy **every** field. |
| `setDefaults()` | First-boot defaults: run, fan 1, heat recovery, everything else off, ID `DEFAULT_DEVICE_ID`. |
| `sameSettings(o)` | Compares user-setting fields only (change detection for NVS). |
| `applyUiMode(m)` | Selects one of the five exclusive modes (ventilation, bypass, clean, auto, AI). Clears the other mode bits; fan, power, lock and heater are untouched. |
| `uiMode()` | Current mode bits → UI mode (priority AI, bypass, clean, auto, ventilation). |
| `setVolume(v)` | Changes fan speed only. |
| `setDeviceId(id)` | Changes the B5 Data1 ID only. |
| `structConverter(d5e5b5)` | D5: outdoor temp, CO2, error. E5: applies non-`FE` fields; fan 0 keeps the current fan (`E5_FAN0_KEEPS_FAN`). Returns `true` if a setting changed. |
| `structConverter(d6b6)` | Heater running, filter remaining / usage. |
| `structConverter(d7e7b7)` | D7: no data. E7: heater, clean, exhaust, supply, sleep, auto. Returns `true` if changed. |
| `structConverter(d8)` / `(d9)` | External sensor values. |
| `structConverter(a6)` | VSP values from MAIN (÷ 2 → tenths of a volt). |
| `makeB5Func(ctrl)` | Builds B5 Data3 from the state. |
| `makeB5Func8()` | Builds B5 Data4 for the function frame (D2 = 8). |
| `makeB5(ctrl)` | Builds the normal B5 with checksum. |
| `dropAiIfOverridden()` *(private)* | Clears AI mode when MAIN sets mode bits that contradict it. |

### 6.2 `PlcService` (`src/rc/PlcService.h`)

| Function | Description |
|---|---|
| `makeChecksum(buf)` | Two's complement of bytes 0–4. |
| `checkChecksum(buf)` | `true` if all 6 bytes sum to 0. |
| `makeB5Time(dto, sel, hours, ctrl)` | Reservation time frame (D2 = 6/7, D4 = hours ≤ 24). |
| `makeB5Function(dto, ctrl)` | Function frame (D2 = 8, D4 = exhaust/supply/dust/sleep). |
| `makeC5(mode, L, M, H)` | VSP change frame (tenths × 2). |
| `makeB6(sensor)` / `makeB7(dto, sensor)` / `makeB8(sensor)` | Sensor replies, clamped to byte ranges; `0x00` when no sample. |
| `toHex(buf, len)` | `"B5 00 01 01 00 49"` for logs. |
| `toBytes(packet, out)` / `fromBytes<T>(in)` | Struct ↔ 6-byte conversion. |

### 6.3 `RcController` (`src/rc/RcController.h`)

Singleton: `RcController::instance()`.

**Commands (called from the UI):**

| Method | Input | Effect |
|---|---|---|
| `setPowerState(on)` | `true` / `false` | On: mode = run, fan 0 → 1. Off: mode = stop, fan kept. Cancels a pending reservation frame. |
| `setModeState(mode)` | `"ventilation"`, `"bypass"`, `"clean"`, `"auto"`, `"ai"` | Mode bits only (via `applyUiMode`). |
| `setVolumeState(v)` | `"1"`–`"3"` | Fan only. 0 is rejected (stop = power off). |
| `setDeviceId(id)` | 1–99 | B5 Data1; saved to NVS. |
| `setReservationState(r)` | `"start_reserve_X"`, `"stop_reserve_X"`, X = 1–24 | mode = 2/3, start/stopTime = X, queues the time frame. |
| `setVspSetting(eH,eM,eL,sH,sM,sL)` | volts as strings, e.g. `"4.8"`, 0.5 < V < 5.5 | Stores VSP, queues one C5 per changed fan. |
| `setFunctionState(func, on)` | `"exhaust"`, `"supply"`, `"sleep"`, `"dust"`, `"heater"`, `"clean"`, `"lock"` | Exhaust/supply/sleep/dust also queue a function frame. |
| `setDataCallback(cb)` | `void(const RcDto&)` | UI refresh callback (UI task only). |
| `setLocalSensors(cache)` | `RC::SensorCache` | RC's own sensor values for B6/B7/B8. |
| `getState()` | – | Copy of the state. |

All commands return `false` on invalid input or while locked.

**Lifecycle / plumbing:**

| Method | Description |
|---|---|
| `init()` | Initialises NVS, loads settings (or defaults). Must run before the first reply. |
| `uiTick()` | UI task, every loop: link status (`plcMsSinceLastFrame() > 5000` → `linkLost`), coalesced UI refresh, NVS save 2 s after the last change. |
| `handleFrame(frame)` | Logs `"<HDR> RX: …"`, dispatches to `structConverter()`, posts a refresh. |
| `buildReply(header, out)` | Builds the reply per section 4.3; `false` = no reply. |
| `notifyUi()` / `dispatchUi()` *(private)* | Post / perform the UI refresh. |
| `saveSettings()` / `loadSettings()` *(private)* | NVS blob, namespace `rc`, key `settings`, version 2. |

**PLC hooks (global functions):**

```cpp
void onPlcFrame(const uint8_t frame[6]);              // valid frame received
bool onPlcReplyDue(uint8_t header, uint8_t out[6]);   // fill the reply, false = none
```

---

## 7. Configuration switches (`src/rc/RcConfig.h`)

| Define | Default | Meaning |
|---|---|---|
| `RC_CTRL_FLAG_ON_LOCAL_CHANGE` | 1 | Set the control flag (B5 D3 bit5) in the first D5 reply after a UI change. |
| `RC_CTRL_FLAG_IN_E5_REPLY` | 0 | Also put a pending control flag in E5 replies. |
| `RC_SEND_C5_ON_VSP_CHANGE` | 1 | Send C5 in place of the next B5 when VSP changes. |
| `RC_SEND_RESERVE_TIME_FRAME` | 1 | Send the reservation time frame (D2 = 6/7). |
| `RC_RESERVE_TIME_AFTER_MODE` | 1 | Time frame after the mode (D2 = 2/3) frame; 0 = before. |
| `RC_RESERVE_TIME_CTRL_FLAG` | 1 | Control flag in the time frame. |
| `RC_SEND_FUNCTION_FRAME` | 1 | Send the function frame (D2 = 8) on exhaust/supply/sleep/dust change. |
| `RC_FUNCTION_FRAME_CTRL_FLAG` | 1 | Control flag in the function frame. |
| `RC_REPLY_TO_D6` / `RC_D6_REPLY_HEADER` | 1 / `B6` | Reply to D6, with B6 or B8. |
| `RC_REPLY_TO_D7` | 1 | Reply B7 to D7/E7. |
| `RC_FILTER_SET_UNIT_H` | 100 | E5 filter time unit (hours). |
| `RC_FILTER_USAGE(hi, lo)` | `hi << 8 \| lo` | How D6 filter-usage bytes combine. |
| `RC_B7_FILTER_ALARM_MASK` | replace \| clean | D5 error bits reported as B7 filter alarm. |
| `E5_FAN0_KEEPS_FAN` | 1 | E5 fan bits = 0 keep the current fan. |
| `AI_SETS_AUTO_BIT` | 0 | Whether AI mode sets the B5 auto bit (AI is not in the protocol). |
| `DEFAULT_DEVICE_ID` | `0x00` | B5 Data1 before the user picks an ID. |
| `DEVICE_ID_MIN` / `DEVICE_ID_MAX` | 1 / 99 | ID picker range. |
| `RC_B6_CO2_DIV` | 20 | B6 CO2 divisor. |
| `RC_LINK_TIMEOUT_MS` | 5000 | "No connection to main unit" after this. |
| `RC_NVS_SAVE_DELAY_MS` | 2000 | NVS save delay after the last change. |

---

### 7.1 Old RC functions (added from SHR-L2 manual)

| Define | Default | Meaning |
|---|---|---|
| `RC_RESERVE_STEP_MIN` | 10 | UI reservation step (minutes) |
| `RC_RESERVE_MAX_MIN` | 1440 | Max reservation (24 h) |
| `RC_RESERVE_TX_UNIT_MIN` | 60 | Minutes per unit in B5 Data4 / E5 Data3 (60 = hours per protocol doc; 10 = old RC steps) |
| `RC_SLEEP_FAN` | 1 | Fan while sleep is on |
| `RC_AUTO_MIN_HOLD_MS` | 10000 | Minimum time between auto-result changes |
| `RC_AUTO_NO_SENSOR_FAN` | 1 | Auto fan when neither CO2 nor PM has a sample |
| `RC_AUTO_PM_USES_PM10` | 1 | Dust level = worse of PM2.5 and PM10 |
| `RC_AUTO_CLEAN_SETS_CLEAN_BIT` | 1 | Auto clean sets the B5 clean bit |
| `RC_SEND_FILTER_FRAME` | 1 | Send B5 Data2 = 5 on filter reset / initial value |
| `RC_FILTER_INIT_DEFAULT_X100` / `MIN` / `MAX` | 30 / 1 / 99 | Filter initial value (×100 h) |

New controller methods: `setVspBypassSetting()`, `setFilterReset()`,
`setFilterInitial()`; `setReservationState()` now takes minutes plus
`"sleep"` and `"cancel"`. New `RcDto` helpers: `isRunning()`, `autoActive()`,
`effectiveVolume()`, `effectiveClean()`. Reply priority after D5 is now
C5 (heat-recovery exhaust, supply, bypass exhaust, supply) → reservation time
→ filter frame → function frame → normal B5. VSP values are stored in 0.05 V
units (the PLC value itself). NVS blob version 3.

## 8. Persistence (NVS)

* Namespace `rc`, key `settings`, blob version **3**. `flags2` also holds 양압
  / 음압 and the "MAIN has the pressure VSP table" flag (old blobs read as 0).
* Key `press` (version 1): 양압 and 음압 setpoints (room, count, supply,
  exhaust, offset, needed, capped) and the "gas warning shown" flag. If it is
  missing, both modes are "not set up".
* Saved: mode, fan, all mode/function flags, AI, lock, heater, reservation
  hours, VSP (6 values), device ID.
* Saved 2 s after the last change (from the UI or from MAIN via E5/E7/A6).
* Loaded in `RcController::init()` before the first reply. Invalid / missing /
  old-version data → first-boot defaults (run, fan 1, heat recovery, ID `0x00`).
  The version bump means the first boot after upgrading starts from defaults.

---

## 9. UI integration

### 9.1 `main.cpp`

* Boot: `RcController::init()` and `setDataCallback(uiRefresh)` run before
  `plc_link::init()` / `plc_protocol::init()`.
* Power key: toggles `setPowerState()`; backlight follows only if accepted
  (ignored while locked).
* `refreshLocalSensors()`: reads the RC's own sensors, updates the bottom
  bar, and feeds `setLocalSensors()`. A missing sample is shown as `-1`.
* `uiRefresh(const RcDto&)` (data callback, UI task):
  * top bar mode text: 통신 끊김 / 정지 / 가동 예약 / 정지 예약 / mode name, plus `LOCK`;
  * filter status: 필터 교체 / 필터 청소 / FAN ERR / 필터 정상 (from `errCode`);
  * outdoor temperature from D5;
  * both dashboards: `set_mode_badge()` and `set_fan_level()`;
  * settings screen: `ui_settings_screen::set_state()`.
* Loop: `if (sensors::poll()) refreshLocalSensors(); RcController::instance().uiTick();`

### 9.2 Settings screen

| Page | Behaviour |
|---|---|
| 풍량 설정 (Airflow) | Row 0: mode – 전열 / 바이패스 / 청정 / 자동 / AI → `setModeState()`. Row 1: fan 약/중/강 → `setVolumeState()`. Left/Right edits, OK sends. Shows the current state (power, mode, fan, LOCK) and the result ("적용됨 – 다음 응답에 전송" or "LOCK – 무시됨"). |
| 예약 설정 (Schedule) | Start / stop in 1–24 hours → `setReservationState()`. |
| 기기 설정 → ID 설정 | Device ID 1–99 (decimal), OK saves → `setDeviceId()`. Shows the current ID and its hex byte. |
| 기기 설정 → VSP 설정 | Shows stored VSP voltages (no voltage editor yet). |

The status strip shows "통신 끊김" while the link to MAIN is lost.

### 9.3 Dashboards

* `ui_main_screen::set_mode_badge(text)` / `set_fan_level(0–3)` – circular-gauge design.
* `ui_main_screen_v2::set_mode_badge(text)` / `set_fan_level(0–3)` – segment-meter
  design (2 lit segments per level, current level label highlighted).

---

## 10. Sensors – SCD40 (CO2) fix

Problem: CO2 never produced a sample (UI showed `-1`).

Causes found in `src/io/sensors.cpp`:

1. The driver used I2C address **0x64**; the Sensirion SCD4x datasheet
   address is **0x62**.
2. "Data not ready" was a silent `return false`, so a sensor that never
   became ready produced no log at all.

Fix:

* `detectScd40()` at boot (after `stop_periodic_measurement` + 500 ms) reads
  the serial number (`0x3682`) at 0x62, then 0x64, and accepts only a
  CRC-valid answer. Logs `SCD4x found at 0x..`, `ACK but bad CRC`, or
  `SCD4x NOT FOUND`.
* `readSCD40()` logs `no data ready for >15s` if the sensor never reports
  ready.

To check: boot log lines `[I2C] scan …` and `[SEN] SCD4x …`, then
`[SEN] SCD40 CO2=… ppm` about every 5 s.

---

## 11. Logging

| Line | Source |
|---|---|
| `D5 RX: D5 2A 08 32 80 …` | every valid frame received (`handleFrame`) |
| `B5 TX: B5 00 01 01 00 49` | every reply sent (`plc_protocol::replyTo`) |
| `RC settings loaded/defaults: mode=… vol=… id=…` | boot |
| `RC settings saved: ok` | NVS save |
| `Device ID set: 98 (0x62)` | ID change |
| `MAIN link lost (> 5000 ms)` / `MAIN link restored` | link status |
| `[SEN] SCD4x …` | CO2 sensor detection / faults |

---

## 12. Verification done

Host-side test of `RcDto` + `PlcService` (g++, `-Wall -Wextra`, no warnings):

| Case | Result |
|---|---|
| First-boot default | `B5 00 01 01 00 49` (matches the line-verified frame) |
| Fan 3, then bypass (fan kept) | `B5 00 01 83 00 C7` |
| Clean (bypass cleared, fan 3 kept) | `B5 00 01 13 00 37` |
| Auto with lock + heater kept | `B5 00 01 4F 00 FB` |
| E5 run, fan bits 0 → fan kept | `B5 00 01 03 00 47` |
| E5 auto, fan 2 → AI dropped | `B5 00 01 0A 00 40` |
| Power off keeps fan 2 | `B5 00 00 0A 00 41` |
| Device ID 98 | `B5 62 01 0A 00 DE` |
| E5 time mode 3 = 5 h → time frame | `B5 00 06 AB 05 95` |
| C5 supply 2.4 / 3.0 / 4.5 V | `C5 02 30 3C 5A 73` |
| A6 exhaust `30 3C 5A` → 24/30/45 (0.1 V) | correct |

---

## 13. Open items

* Confirm every `RcConfig.h` switch on the line (control flag, reservation
  frame order, function frame, D6 → B6/B8, filter-usage format).
* After choosing a device ID, confirm MAIN still obeys a fan change; MAIN's
  own ID setting must match.
* No UI yet for lock, heater, exhaust, supply, sleep, dust, or VSP voltage
  editing (controller methods exist).
* AI mode is UI / state only; no AI logic and not sent to MAIN
  (`AI_SETS_AUTO_BIT = 0`).
* If MAIN sends an E5 changing the same field before the next B5, MAIN's
  value wins and the local change is lost.
* B6 sends CO2 = 0 before the first SCD40 sample (no "no data" value in the
  protocol).
* The old `plc_protocol` `request*()` / `desired()` API is now unused but left
  in place.
* The 140 ms blocking reply delay in `plc_protocol` slows UI refresh.
* Pressure modes: confirm on the line that MAIN applies a C5 at once and
  whether it stores it in its own memory. Also confirm the real fan curve
  (`RC_FAN_VSP_AT_ZERO` / `RC_FAN_VSP_AT_RATED`) and the rated airflow
  (`RC_FAN_RATED_M3H`, placeholder 500 m³/h), and the room table values.
