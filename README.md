# CendoriRC – ERV Room Controller Firmware

Firmware for the **CendoriRC room controller** (board CD-WD-RMC-V1.0-260811,
SL Technologies): a 5" touch-key wall panel that controls an ERV / ventilation
main unit (**MAIN**) over a **PLC (power-line) link**, shows indoor air quality
from its own sensors, and stores the user's settings.

| Item | Value |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB octal PSRAM) |
| Display | T050800480-B8WMN-037, 5.0" 800×480, ST7262, RGB565 parallel |
| Framework | **ESP-IDF** (via PlatformIO), migrated from Arduino |
| UI | LVGL 8.3 |
| Link to MAIN | PLC, UART2, 2400 baud 8N1, GPIO19 TX (Q5) / GPIO20 RX (Q3) |
| Sensors (I2C, GPIO40/41) | SHT40 temp/RH, SGP40 VOC, SCD40 CO2, PM2012 dust, MCP7940 RTC, touch IC; CO cell on ADC GPIO8 |
| Branch | `espidf-migration` (baseline Arduino build = commit `bfa3e79`) |

Detailed design and function reference for the RC/PLC layer:
[`docs/rc_plc_layer.md`](docs/rc_plc_layer.md).

---

## 1. Directory structure

```
CendoriRC/
├── README.md                     This file
├── platformio.ini                PlatformIO env: ESP-IDF framework, COM4, LVGL + Sensirion VOC lib
├── CMakeLists.txt                ESP-IDF project file (+ -Os workaround for esp_lcd_panel_rgb.c)
├── sdkconfig.defaults            Our IDF settings: octal PSRAM, 8 MB flash, 1 kHz tick, USB-JTAG off
├── sdkconfig.esp32-s3-devkitc-1  Full generated IDF config (do not hand-edit; edit sdkconfig.defaults)
├── .gitignore
│
├── docs/
│   └── rc_plc_layer.md           Full documentation of the RC state / PLC protocol layer
│
├── include/
│   ├── lv_conf.h                 LVGL configuration (tick from esp_timer)
│   └── README                    PlatformIO placeholder
│
├── scripts/
│   └── fix_lang_flags.py         PlatformIO pre-build script: moves C-only / C++-only flags
│                                 to the right compiler (fixes -Werror build break with ESP-IDF)
│
├── tools/
│   └── gen_fonts.sh              Regenerates the Korean subset fonts (lv_font_conv + Malgun Gothic)
│
├── lib/  test/                   PlatformIO placeholders (README only)
│
└── src/
    ├── CMakeLists.txt            Registers every .c/.cpp under src/ as the app component
    ├── main.cpp                  Entry point (app_main), LCD + LVGL setup, button dispatch,
    │                             dashboard refresh, main loop
    │
    ├── rc/                       ── RC state layer (new) ──
    │   ├── RcConfig.h            All unconfirmed protocol details as named #defines
    │   ├── RcAuto.h/.cpp         Air-quality levels (old RC Tables 1-3) + auto-mode decision (Table 4)
    │   ├── RcDto.h/.cpp          RC state: user settings + values from MAIN; frame → state
    │   │                         (structConverter), state → B5 (makeB5), mode/fan/ID helpers
    │   ├── PlcService.h/.cpp     Frame builders: checksum, B5 variants, B6, B7, B8, C5, hex log
    │   └── RcController.h/.cpp   Single owner of the state (mutex): UI commands, PLC hooks,
    │                             reply priority, link status, NVS save/load, UI refresh
    │
    ├── io/                       ── Hardware / protocol drivers ──
    │   ├── PLCStruct.h           Internal-protocol frame structs and bit-mask enums
    │   ├── plc_link.h/.cpp       PLC UART2 transport: Q5 safety, inverted RX/TX, XOR 0xFF,
    │   │                         timestamped RX ring, own-echo removal
    │   ├── plc_protocol.h/.cpp   Frame sync (header + checksum), dispatch to RcController,
    │   │                         140 ms reply timing, link timer, diagnostics
    │   ├── ahu_link.h/.cpp       RS-485 transport (UART1, GPIO17/18/48) – diagnostics only
    │   ├── ahu_protocol.h/.cpp   RS-485 protocol – control removed (bus doesn't reach MAIN)
    │   ├── sensors.h/.cpp        Round-robin I2C sensor polling (SHT40, SGP40+VOC index,
    │   │                         SCD40, PM2012, RTC, CO ADC)
    │   ├── touch_buttons.h/.cpp  8 capacitive keys via touch IC 0x24 (Back, Ok, arrows,
    │   │                         Setting, Power)
    │   └── backlight.h/.cpp      LEDC backlight with soft-start ramp (protects the PLC 5 V rail)
    │
    ├── port/                     ── Arduino → ESP-IDF shims (new) ──
    │   ├── esp_time.h            millis() / delay() / delayMicroseconds() over esp_timer/FreeRTOS
    │   ├── i2c_bus.h/.cpp        Shared I2C master (replaces Wire)
    │   └── co_adc.h/.cpp         CO-cell ADC with calibration (replaces analogReadMilliVolts)
    │
    └── ui/                       ── LVGL screens ──
        ├── ui_theme.h            Colour palette
        ├── ui_icons.h/.cpp       Vector icons drawn with LVGL primitives
        ├── ui_dashboard_chrome.h/.cpp  Shared top status bar + bottom sensor strip
        ├── ui_main_screen.h/.cpp       Dashboard A: AQI + circular fan gauges, mode badge
        ├── ui_main_screen_v2.h/.cpp    Dashboard B: 24 h trend + segmented fan meters, mode badge
        ├── ui_settings_screen.h/.cpp   Settings: airflow, schedule, Wi-Fi, engineer (ID, VSP), filter
        └── fonts/
            ├── ui_fonts.h        Font declarations
            └── ui_font_xs/sm/md/xxl.c  Malgun Gothic subsets (14/18/22/34 px, Latin + used Hangul)
```

---

## 2. Workflow

### 2.1 Build, flash, monitor

```bash
pio run                       # build
pio run -t upload             # flash over COM4 (esptool, 115200)
pio device monitor            # serial log, 115200, DTR/RTS off
```

* Close the serial monitor before flashing (COM4 can only be opened once).
* Change IDF options in `sdkconfig.defaults`, not in the generated `sdkconfig.*`.
* After adding new Korean text to the UI, add its code points to
  `tools/gen_fonts.sh` and run `bash tools/gen_fonts.sh`; otherwise the glyphs
  render blank.

### 2.2 Boot sequence (`main.cpp`)

1. `logResetReason()`, then the app task (16 KB stack, core 1).
2. `plc_link::holdQ5Off()` – GPIO19 LOW before anything else (Q5 on would kill PLC 5 V and the LCD).
3. Backlight at 50 % with soft-start ramp.
4. I2C bus, RGB LCD panel (PSRAM frame buffer + bounce buffer), LVGL, screens.
5. Touch keys, sensors (I2C scan, SCD40 detection), RS-485 diagnostics bring-up.
6. `RcController::init()` – load settings from NVS **before the first reply**; register the UI callback.
7. `plc_link::init()`, `plc_protocol::init()` – start listening to MAIN.

### 2.3 Main loop (`appLoop`, every ~5 ms)

```
touch::poll()                → button press → screen navigation / RcController commands
plc_protocol::poll()         → frames from MAIN → RcController::handleFrame()
                             → 140 ms later → RcController::buildReply() → plc_link TX
backlight::tick()            → soft-start ramp
sensors::poll()              → refreshLocalSensors() → bottom bar + RcController sensor cache
RcController::uiTick()       → link status, coalesced UI refresh, NVS save (2 s after change)
lv_timer_handler()           → LVGL draw
```

### 2.4 Control flow between UI, RC and MAIN

```
User key / settings OK
   └─► RcController::set*()  ──► RcDto changed (mutex) ──► notifyUi()
                                        │
MAIN sends D5/E5/D6/D7/E7 ──► plc_protocol ──► handleFrame() ──► RcDto updated ──► notifyUi()
                                        │
                            140 ms later: buildReply(header)
                                        │
                     C5 / time B5 / function B5 / normal B5 / B6 / B7 ──► MAIN
```

* **MAIN always speaks first; the RC only answers.** A UI action never transmits;
  the change goes out in the next reply.
* **The screen shows only `RcDto`**, which is exactly what the next B5 carries.
* While **LOCK** is on, every command except unlock is ignored.
* No valid frame for 5 s → "통신 끊김" (no link) shown; controls keep working and are
  sent when the link returns.

### 2.5 Physical keys

| Key | Dashboard | Settings |
|---|---|---|
| Power | Power on/off (`setPowerState`), backlight follows | same |
| Setting | Open settings | – |
| Left / Right | Switch dashboard A / B | Change the focused value |
| Up / Down | – | Move between menu items / rows |
| OK | – | Enter panel / send the focused row / save |
| Back | – | Cancel / leave panel / leave settings |

---

## 3. Updates done (complete list)

Baseline: commit `bfa3e79` – working Arduino-framework firmware. Everything
below is on `espidf-migration` and **not yet committed**.

### 3.1 Arduino → ESP-IDF migration

| Change | Files |
|---|---|
| Framework switched from Arduino to ESP-IDF; Arduino GFX library removed; PSRAM / flash / CPU / tick / USB settings moved into `sdkconfig.defaults` (octal PSRAM, 8 MB flash, 160 MHz, 1 kHz FreeRTOS tick, size optimisation, USB-Serial-JTAG off). | `platformio.ini`, `sdkconfig.defaults`, `sdkconfig.esp32-s3-devkitc-1` |
| IDF project + component CMake files; `-Os` on `esp_lcd_panel_rgb.c` to avoid a GCC 15 internal compiler error on Windows. | `CMakeLists.txt`, `src/CMakeLists.txt` |
| Build-flag fix script for mixed C/C++ sources under `-Werror`. | `scripts/fix_lang_flags.py` |
| LVGL tick from `esp_timer` instead of Arduino `millis()`. | `include/lv_conf.h` |
| Arduino shims: `millis/delay`, shared I2C bus (replaces Wire), CO ADC (replaces `analogReadMilliVolts`). | `src/port/*` |
| LCD driven by IDF `esp_lcd` RGB panel (16 MHz PCLK, PSRAM frame buffer, 10-line bounce buffer against tearing); LVGL flush wired to it; app runs in its own 16 KB task; reset reason logged at boot. | `src/main.cpp` |
| Drivers ported: touch keys, sensors, backlight, RS-485 link. | `src/io/*` |

### 3.2 Hardware / power fixes

| Change | Files |
|---|---|
| GPIO19 (PLC TX → Q5) held LOW from the earliest constructor and on every init; USB-JTAG pads disabled (USB on GPIO19/20 turned Q5 on and killed PLC 5 V / the LCD). | `plc_link.cpp`, `sdkconfig.defaults` |
| Backlight soft-start ramp so the boost converter's inrush doesn't stack with the PLC rectifier's and blank the panel. | `backlight.cpp/.h` |

### 3.3 PLC link to MAIN (internal protocol)

| Change | Files |
|---|---|
| New PLC transport on UART2 2400 8N1: RX inverted + XOR 0xFF (line idles LOW); TX inverted so idle keeps Q5 off; TX pin bound to the UART only while sending. | `plc_link.cpp/.h` |
| Dedicated UART event task timestamps every byte on arrival (real wire timing); 512-entry RX ring. | `plc_link.cpp` |
| Own-TX echo removal: only bytes timestamped inside TX + 20 ms are dropped (the old blanket flush also destroyed real bytes); echo compared with what was sent. | `plc_link.cpp` |
| Frame layer: 6-byte sliding window, known header + two's-complement checksum, burst timing / health / byte-count diagnostics, link timer `plcMsSinceLastFrame()`. | `plc_protocol.cpp/.h` |
| Frame structs and bit enums for D5/E5/B5, D6/B6, D7/E7/B7, D8, B8, D9, A6, C5. | `PLCStruct.h` (+ `FRAME_LEN`, `DEVICE_ID`, size checks) |
| Replies built from live RC state instead of the fixed `B5 00 01 01 00 49`; replies now also sent to D6 (B6) and D7/E7 (B7). | `plc_protocol.cpp` |
| RS-485 control path removed (confirmed it doesn't reach MAIN); UART1 kept for passive diagnostics only. | `ahu_protocol.*`, `ahu_link.*` |

### 3.4 RC state layer (new, `src/rc/`)

| Feature | Details |
|---|---|
| Shared state `RcDto` | mode, fan 0–3, bypass, heater, clean, auto, lock, exhaust, supply, sleep, dust, AI, device ID, start/stop reservation (≤ 24 h), VSP exhaust/supply L/M/H (0.1 V); received outdoor temp, MAIN CO2, error/alarm bits, filter remaining/usage, heater running, fire/bath/hood flags, external D8/D9 sensors, bypass-fan VSP; link status. Full copy ctor / `operator=`. |
| MAIN → state | `structConverter()` for D5, E5 (non-`FE` fields only), D6, D7/E7, D8, D9, A6. Only E5/E7 change settings. |
| State → MAIN | Normal B5, reservation time B5 (D2 = 6/7), function B5 (D2 = 8), C5 per changed fan, B6 / B7 / B8 from the RC's own sensors. |
| Reply priority after D5 | C5 → reservation time → function frame → normal B5 (control flag once after a local change). E5 always gets a B5 with the state *after* the E5. |
| UI commands | `setPowerState`, `setModeState`, `setVolumeState`, `setReservationState`, `setVspSetting`, `setFunctionState`, `setDeviceId`, `setDataCallback`, `setLocalSensors`, `getState`. Input validation; lock blocks all but unlock. |
| Threading | FreeRTOS mutex held only for field copy/change; `notifyUi()` posts one coalesced refresh handled by `uiTick()` in the UI task; PLC path never touches widgets. |
| Persistence | NVS blob (namespace `rc`, version 2), saved 2 s after the last change, loaded before the first reply; first-boot defaults: run, fan 1, heat recovery, ID 0x00. |
| Link status | > 5 s without a valid frame → `linkLost` → "통신 끊김". |
| Config | Every unconfirmed protocol detail behind a `#define` in `RcConfig.h`. |
| Logging | `"<HDR> RX: …"` per received frame, `"<HDR> TX: …"` per reply. |

### 3.5 Additional functions (spec section 9)

| Function | Details |
|---|---|
| Mode and fan independent | Mode change touches only mode bits; fan change touches only the fan; power off keeps the fan and power on restores it; UI fan is 1–3 only; E5 fan bits 0 keep the current fan (`E5_FAN0_KEEPS_FAN`). |
| One mode column | Ventilation / Bypass / Clean / Auto / **AI**, mutually exclusive (`applyUiMode`). AI is UI/state only for now (`AI_SETS_AUTO_BIT = 0`, not sent to MAIN); dropped automatically if MAIN sets contradicting mode bits. |
| Device ID 1–99 | Picker in Engineer → ID 설정; value used as-is in B5 Data1 (98 → `0x62`), checksum rebuilt; saved to NVS; default `0x00` until chosen. |
| Live mode / fan on the main screen | Both dashboards show the mode badge (or 정지) and fan level from `RcDto` only, refreshed after every change from any source. |

### 3.6 UI changes

| Screen | Change |
|---|---|
| Dashboards (A and B) | New `set_mode_badge()` and `set_fan_level()`; dashboard B fan meters are now live instead of a fixed demo level. |
| Top bar | Mode text (통신 끊김 / 정지 / 가동 예약 / 정지 예약 / mode name + LOCK); filter/fan status from MAIN's error bits; outdoor temperature from D5. |
| Bottom bar | RC's own sensors only; a missing sample shows `-1`. |
| Settings → Airflow | 5-way mode row + fan 약/중/강; OK sends via the controller; shows current state and "queued" / "LOCK ignored" result. Old RS-485 send/confirm/cooldown logic and the separate clean row removed. |
| Settings → Schedule | Start/stop reservation in 1–24 hours (was a clock time); sends via the controller. |
| Settings → Engineer → ID | 1–99 device ID picker (was RS-485 ID 0–15). |
| Settings → Engineer → VSP | Shows the stored VSP voltages from the RC state. |

### 3.7 Sensors

Already in the baseline (unchanged in behaviour, only ported to the IDF I2C
shim): SGP40 with the Sensirion VOC Gas Index Algorithm, PM2012 on I2C 0x28,
SCD40 stop-then-start at boot.

| Change | Details |
|---|---|
| Port | Wire / `analogReadMilliVolts` replaced by `port/i2c_bus` and `port/co_adc`. |
| SCD40 (CO2) | **Address fix:** auto-detects 0x62 (datasheet) or 0x64 via a CRC-checked serial-number read; logs `SCD4x found / bad CRC / NOT FOUND`; logs "no data ready for > 15 s" instead of failing silently. |
| Sensor cache to RC | `refreshLocalSensors()` feeds the RC's own readings to B6/B7/B8; a missing sample is flagged, shown as `-1`, and sent as `0x00`. |

### 3.8 Old RC (SHR-L2) functions

The room controller's functions (reservation, modes and auto mode, filter,
error codes, VSP, heater, ID, outdoor temperature) are described in
[`docs/rc_function_spec.md`](docs/rc_function_spec.md), which follows the
structure of the SHR-L2 approval document. The screens, keys and messages
are described in [`docs/rc_ui_guide.md`](docs/rc_ui_guide.md).

Fonts were regenerated with 6 new glyphs (값 류 여 잔 침 히) via
`tools/gen_fonts.sh`. The NVS settings blob is now version 3 (reservation
minutes, filter initial value, bypass VSP), so the first boot after upgrading
starts from defaults.

### 3.9 Documentation

* `docs/rc_plc_layer.md` – full RC/PLC design and function reference.
* `README.md` – this file.

---

## 4. Verification status

| Item | Status |
|---|---|
| Firmware build | ✅ builds cleanly |
| Frame builders (B5 variants, C5, B6, B7, A6/D5/E5 parsing, mode/fan/ID cases) | ✅ host test, expected bytes match (see `docs/rc_plc_layer.md` §12) |
| Default reply `B5 00 01 01 00 49` | ✅ MAIN acts on it (line-verified earlier) |
| Device ID, mode/fan independence, AI, main-screen live values | ⏳ not yet tested on hardware |
| SCD40 address detection | ⏳ not yet tested – check `[SEN] SCD4x …` in the boot log |

## 5. Open items

* Reservation time unit on the wire (`RC_RESERVE_TX_UNIT_MIN`): the protocol
  document says whole hours; the old RC used 10-minute steps. Check on the line.
* Filter frame (B5 Data2 = 5, Data4 = initial value) is unconfirmed.
* Sleep has no timer yet (the old RC manual shows a time on the sleep screen
  without describing it).
* VOC levels (Table 1) are in ppb (old SGP30); our SGP40 gives an index, so
  VOC is not used by auto mode.

* Confirm each `RcConfig.h` switch on the line (control flag, reservation frame
  order, function frame, D6 → B6/B8, filter-usage format).
* After choosing a device ID, confirm MAIN still obeys a fan change (MAIN's ID must match).
* No UI yet for lock, heater, exhaust, supply, sleep, dust, VSP voltage editing.
* AI mode has no logic yet and is not sent to MAIN.
* An E5 from MAIN changing the same field before the next B5 overrides a local change.
* B6 reports CO2 = 0 before the first SCD40 sample (protocol has no "no data" value).
* `plc_protocol`'s old `request*()` / `desired()` API is unused but still present.
* The 140 ms blocking reply delay in `plc_protocol` slows UI refresh.
* All work is uncommitted on `espidf-migration`.
#   C e n d o r i R c  
 