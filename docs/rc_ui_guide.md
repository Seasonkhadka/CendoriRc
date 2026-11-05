# CendoriRC 룸 콘트롤러 화면 설명서

UI guide – screens, keys and messages of the room controller

| 버전 | 작성일 | 변경내용 |
|---|---|---|
| 1.0 | 2026-10-23 | 최초작성 (First version) |

---

## 목 차

1. 화면 구성
2. 대시보드
   - 2.1 상단 바
   - 2.2 대시보드 A
   - 2.3 대시보드 B
   - 2.4 하단 바
   - 2.5 대시보드 C
3. 버튼 동작
4. 설정 화면
   - 4.1 공통 구성
   - 4.2 풍량 설정 (양압 / 음압 설정 마법사)
   - 4.3 예약 설정
   - 4.4 네트워크 설정
   - 4.5 기기 설정 (ID 설정, VSP 설정)
   - 4.6 필터 상태
   - 4.7 언어 설정
5. 표시 메시지

---

## 1. 화면 구성

1.1 Display: 5.0" LCD, 800 × 480.

1.2 The room controller has four screens:

| 화면 | 설명 | 여는 방법 |
|---|---|---|
| 대시보드 A | Air quality and fan gauges | ◀ / ▶ on a dashboard (A → B → C → A) |
| 대시보드 B | Trend graph and fan level meters | ◀ / ▶ on a dashboard |
| 대시보드 C | Light design: AQI ring, airflow picture, AI ventilation card | ◀ / ▶ on a dashboard |
| 설정 | Settings menu | 설정 key on the dashboard |

1.3 대시보드 A and B share the same top bar and bottom bar (2.1, 2.4).
대시보드 C has its own light top bar and sensor pills (2.5).

1.4 Every value on the screen shows the current state of the room
controller, which is what is sent to the main unit in the next reply.

---

## 2. 대시보드

### 2.1 상단 바

| No. | 표시 | 설명 |
|---|---|---|
| (1) | Wi-Fi 표시 | Wi-Fi icon and 연결됨 / 미연결 (see 2.1.1) |
| (2) | 외기 온도 | "외기 N°C", outdoor temperature from the main unit |
| (3) | 시계 | "AM / PM hh:mm", current time from the RTC (see 2.1.3) |
| (4) | 상태 표시 | Error code or filter state (see 2.1.2) |

2.1.1 Wi-Fi 표시 – the Wi-Fi icon and "연결됨" (Connected) or "미연결"
(Not connected). The icon is dimmed while not connected. There is no Wi-Fi
function yet, so it always shows 미연결. The operating mode is not shown in
the top bar; it is shown by the mode badge on the dashboards.

2.1.2 상태 표시 – shown in this order of priority:

| 표시 | 색 | 의미 |
|---|---|---|
| E3 | Warning | Supply and exhaust fan error |
| E1 | Warning | Supply fan error |
| E2 | Warning | Exhaust fan error |
| FAN ERR | Warning | Outdoor-air fan error |
| 필터 교체 | Warning | Replace the filter |
| 필터 청소 | Warning | Clean the filter |
| 필터 정상 | Normal | No error |

2.1.3 시계 – The time comes from the RTC. On the first boot of a new
firmware, and whenever the RTC is behind the firmware build time, the RTC is
set to the build time (the build PC's clock). Flash right after building:
the clock is behind by the time between build and boot.

### 2.2 대시보드 A

| No. | 표시 | 설명 |
|---|---|---|
| (1) | AQI 카드 | AQI value and level (e.g. "35 / 보통") with a ring gauge in the level colour (see 2.2.3) |
| (2) | 실내 온습도 | Indoor temperature and humidity in the middle of the ring |
| (3) | 모드 배지 | Current mode (see 2.2.1) |
| (4) | 급기 / 배기 게이지 | Fan level running now: 정지 / 약 / 중 / 강 |
| (5) | 예약 표시 | Reservation line under the gauges |

2.2.1 모드 배지

| 표시 | 조건 |
|---|---|
| 정지 (OFF) | Power Off |
| 자동 청정 (AUTO CLEAN) | Auto mode running in clean |
| 전열교환 (HEAT RECOVERY) | 전열 mode |
| 바이패스 (BYPASS) | 바이패스 mode |
| 청정 (CLEAN) | 청정 mode |
| 자동 (AUTO) | 자동 mode |
| AI 모드 (AI) | AI mode |

2.2.2 The fan gauges show the level that is actually running: low in sleep
mode, and the automatic result in auto mode.

2.2.3 AQI (실내 공기질 지수), same calculation as the app (`src/rc/AirIndex`):

- Each pollutant gets a 0–100 score on its own line (0–25 / 25–50 / 50–75 /
  75–100, linear between the cuts, 2 decimals):

  | 오염물질 | 1st cut | 2nd cut | 3rd cut | max |
  |---|---|---|---|---|
  | PM10 (㎍/㎥) | 31 | 81 | 151 | 5000 |
  | PM2.5 (㎍/㎥) | 16 | 36 | 76 | 5000 |
  | TVOC (㎍/㎥) | 201 | 401 | 1001 | 10000 |
  | CO2 (ppm) | 700 | 1001 | 2001 | 10000 |
  | CO (ppm) | 2 | 10 | 30 | 200 |

- Scores of 0 (value 0 or no reading) are left out; the rest are averaged
  without rounding. TVOC (`RC_AQI_USE_TVOC`) and CO (`RC_AQI_USE_CO`) are left
  are used: TVOC is ㎍/㎥ converted from the SGP40 VOC index (÷ 1000 for the
  mg/㎥ table) and CO is ppm from the ME2-CO cell (sensitivity not confirmed).
- Label and colour (colours lifted for the dark background):

  | 평균 | 표시 | 색 |
  |---|---|---|
  | 0 – 25.00 | 좋음 (Good) | blue #3D7BFF (app #0341cd) |
  | 25.01 – 50.00 | 보통 (Moderate) | green #22C55E (app #00ac38) |
  | 50.01 – 75.00 | 나쁨 (Bad) | orange #FF7A1A (app #ff6000) |
  | 75.01 – 100 | 매우 나쁨 (Very bad) | red #FF4545 (app #d90000) |
  | no sensor reading | - | gray #8A93A6 (app #aaaaaa) |

- The number is the average rounded up (25.01 → 26), so it always matches the
  label. The ring fills more when the air is cleaner (100 − average) and takes
  the level colour; the label text uses the same colour.
- The reservation line is still a fixed display value.

### 2.3 대시보드 B

| No. | 표시 | 설명 |
|---|---|---|
| (1) | 추이 그래프 | Indoor temperature graph with hour labels; the bubble shows the current temperature and humidity |
| (2) | 모드 배지 | Same as 2.2.1 |
| (3) | 급기 / 배기 레벨 | 6-segment meters; 2 segments per level (약 / 중 / 강), the current level label is highlighted |
| (4) | 예약 표시 | Reservation line under the meters |

2.3.1 The AQI header ("AQI 35 / 보통") shows the same value, label and colour
as 2.2.3. The graph curve and the reservation line are fixed display values
and are not connected to data yet.

### 2.4 하단 바

| 카드 | 단위 | 설명 |
|---|---|---|
| PM2.5 | ㎍/㎥ | Fine dust (PM2012) |
| PM10 | ㎍/㎥ | Dust (PM2012) |
| CO2 | ppm | CO2 (SCD40) |
| TVOC | ㎍/㎥ | TVOC (SGP40, converted from the VOC index; "--" for ~45 s after power-on) |
| CO | ppm | Carbon monoxide (ME2-CO; "--" for 60 s warm-up) |
| Temp | °C / % | Indoor temperature and humidity (SHT40) |

"--" is shown when a sensor has no value.

### 2.5 대시보드 C

Light ("glass") design. The background, the airflow picture and the ring
colours are pictures stored in the firmware (`tools/gen_dash_c_art.py`);
everything else is live.

| No. | 표시 | 설명 |
|---|---|---|
| (1) | 상단 바 | Same elements and size as the 대시보드 A/B top bar (2.1): Wi-Fi, weather, date and time, filter status. Light frosted bar, dark text; filter status teal when normal, red otherwise |
| (2) | AQI | "AQI 32 EXCELLENT": AQI 0~500 and level (EXCELLENT / MODERATE / POOR / VERY POOR), in the level colour |
| (3) | AQI 링 | Ring colour by level (blue / green / orange / red); fuller ring = cleaner air (100 − AQI ÷ 5 %) |
| (4) | 실내 온습도 | Indoor temperature and humidity inside the ring (SHT40) |
| (5) | 급기 / 배기 그림 + VSP | Airflow picture (fixed). Under each fan name: that fan's VSP voltage for the current fan level and 전열/바이패스 set, e.g. "2.40V" (value only). "--" when that fan is not running (unit stopped, 배기모드 = supply fan off, 급기모드 = exhaust fan off) |
| (6) | 모드 버튼 | One button with the current mode: 전열 모드 / 바이패스 / 청정모드 / 자동모드 / AI 모드 / 양압 모드 / 음압 모드 / 바이패스 양압 / 바이패스 음압. In 양압 / 음압: "양압 모드 +8%" and a small second line "급기 320 · 배기 295 m³/h" ("CO₂ 부스트 384 / 354 m³/h" during the boost). Lit while running, dim when stopped. Display only |
| (7) | 풍량 | One fan row (the main unit has one shared fan speed). Bar = fan level (약 / 중 / 강 = 1/3, 2/3, full), level name on the right (정지 / 약 / 중 / 강). Red icon and "오류" on a supply or exhaust fan error from the main unit |
| (8) | 예약 | 가동예약 / 정지예약 and the reservation time, or 예약 없음 |
| (9) | 하단 카드 | Same six cards as the 대시보드 A/B bottom bar (2.4): PM2.5, PM10, CO₂, TVOC, CO, Temp (temperature and humidity), same units. Pastel pills, no shadow |

---

## 3. 버튼 동작

| 버튼 | 대시보드 | 설정 화면 |
|---|---|---|
| 전원 | Power On / Off (backlight follows) | Same |
| 설정 | Open the settings screen | – |
| ◀ | Previous dashboard (C → B → A → C) | Change the value of the selected row |
| ▶ | Next dashboard (A → B → C → A) | Change the value of the selected row |
| ▲ / ▼ | – | Move between menu items or rows |
| OK | – | Enter a menu / send or save |
| Back | – | Cancel / leave a menu / close settings |
| ▲ + ▼ (3초) | Heater function On / Off | – |

---

## 4. 설정 화면

### 4.1 공통 구성

| No. | 표시 | 설명 |
|---|---|---|
| (1) | 경로 표시 | "설정 (SETTINGS) > menu name"; "기기설정" with a lock icon inside Engineer Mode. Current time and Wi-Fi icon on the right |
| (2) | 메뉴 | Left side menu list |
| (3) | 내용 | Right side panel of the selected menu |
| (4) | 버튼 안내 | Footer with the key hints for the current screen |

4.1.1 메뉴

| 메뉴 | 내용 |
|---|---|
| 풍량 설정 (Airflow) | Mode and fan level |
| 예약 설정 (Schedule) | Reservation and sleep |
| 네트워크 설정 (Wi-Fi) | Wi-Fi |
| 기기 설정 (Engineer Mode) | ID and VSP (lock icon) |
| 필터 상태 (Filter) | Filter time and initial value |
| 언어 설정 (Language) | 한국어 / English |

4.1.2 사용 방법

1. ▲ / ▼ select a menu. The panel on the right shows its contents.
2. OK enters the panel. A frame marks the selected row.
3. ▲ / ▼ move between rows, ◀ / ▶ change the value.
4. OK sends or saves. Back leaves the panel without saving.
5. Back on the menu list closes the settings screen.

### 4.2 풍량 설정

| 행 | 내용 |
|---|---|
| 현재 상태 | "현재 상태 (State): ON / Ventilation / Fan 2", with " / SLEEP" and " / LOCK" when active. In 양압 / 음압 a second line: "양압 모드 +8%  급기 320 m³/h · 배기 295 m³/h", plus " · CO₂ 부스트 동작 중" while the boost runs |
| 모드 | 전열 (Vent) / 바이패스 (Bypass) / 청정 (Clean) / 자동 (Auto) / AI. The picked mode is the only white pill (no other indicator) |
| 전열 방식 / 바이패스 방식 | Only when 전열 or 바이패스 is picked: 일반 (Normal, balanced) / 양압 (Positive) / 음압 (Negative) on that air path. 양압 has arrows pointing out of a room, 음압 arrows pointing in |
| 풍량 | 약 (Low) / 중 (Mid) / 강 (High) |
| 양압/음압 다시 설정 | Only when 전열 or 바이패스 is picked. Plain text: 양압 or 음압 with its saved offset (e.g. "양압 +8%") or "미설정 (Not set)"; shown as "< 양압 +8% >" while the row is focused |
| 상태 줄 | Result of the last command (see 5) |

4.2.1 ◀ / ▶ pick a value in the selected row. OK applies only that row and
the panel stays open, so the other rows can be set too.

4.2.2 The mode can be changed only while the power is On. The fan level can
be changed only in manual mode (not in 자동, 취침, 양압 or 음압).

4.2.3 OK on the 모드 row or the 방식 row applies 전열 / 바이패스 with the picked
방식 (일반 = the normal mode). A wizard started from 바이패스 starts 바이패스 양압 /
음압 on 적용.

4.2.4 양압 / 음압 설정 마법사: OK on 양압 or 음압 before it was ever set up, or OK
on "양압/음압 다시 설정", opens the wizard in the same panel.

| 단계 | 화면 | 버튼 |
|---|---|---|
| 1 / 3 | 공간 종류: e.g. 개방형 사무실 / 회의실 / 개인 사무실, with "1인당 32 m³/h · 양압 모드 +8%" | ◀ / ▶ room, OK next, ↩ close |
| 2 / 3 | 인원 (명) or 변기 수 (개): [-] 10 명 [+] | ◀ / ▶ or ▲ / ▼ count, OK next, ↩ back |
| 3 / 3 | 결과: "급기 320 m³/h · 배기 295 m³/h", "양압 모드 +8%", warnings, [적용] | OK apply, ↩ back |

Nothing is sent before 적용. 적용 saves the setpoints and starts the mode. If
the power is off or the unit is locked, the setpoints are only saved.

### 4.3 예약 설정

| 행 | 내용 |
|---|---|
| 예약 종류 | Pills: 가동 예약 (Start) / 정지 예약 (Stop) / 취침 (Sleep) / 취소 (Cancel); the selected one is the glowing white pill |
| 예약 시간 | Large stepper "[-]  1:30 후  [+]" in 10-minute steps, up to 24:00; "--:--" and dimmed keys for 취침 and 취소 |
| 안내 줄 | When it will act, e.g. "PM 06:22 자동 정지 예약 / Auto Off at 18:22" (from the current time); "취침 모드 - 약풍" or "예약 취소" for the other types |
| 상태 줄 | Result of the last command (see 5) |

- ▲ / ▼ move between 예약 종류 and 예약 시간.
- ◀ / ▶ change the type or the time.
- OK (예약 시작) sends the reservation from either row and leaves the panel.
- Back cancels.

4.3.1 Sleep mode always runs on low fan.

4.3.2 취소 cancels a waiting reservation and ends sleep mode.

### 4.4 네트워크 설정

| 행 | 내용 |
|---|---|
| 네트워크 | Network name and signal bars (0–4), or "네트워크 없음" |
| 연결 상태 | 연결됨 / 연결 중... / 연결 실패 / 미연결 |
| 실패 안내 | e.g. "'Home' 연결 실패 - 비밀번호를 확인하세요" (wrong password) or "네트워크를 찾을 수 없음" (not found), kept until the next try |
| IP / MAC | IP address ("-" when not connected) and this board's MAC address |
| 스마트폰 간편 연결 | QR setup (4.4.2) |
| 주변 Wi-Fi 검색 | Network list + password keyboard (4.4.3) |
| 네트워크 초기화 (Disconnect) | Disconnects and forgets the saved network |

In the panel: ◀ / ▶ (or ▲ / ▼) select one of the three buttons, OK runs it,
Back leaves the panel.

4.4.2 스마트폰 간편 연결 (QR setup)

1. OK on 스마트폰 간편 연결. The RC starts its own Wi-Fi network
   `CENDORI-RC-xxxx` (password `cendorixxxx`, xxxx = end of the MAC address)
   and shows a QR code with the network name and password.
2. Scan the QR code with the phone camera; the phone joins that network and a
   setup page opens (if not, browse to `192.168.4.1`).
3. On the page, pick the home Wi-Fi (or type a hidden network name), enter the
   password and press 연결 (Connect).
4. The screen shows 휴대폰 연결됨 → 연결 중: name → 연결됨: name. After
   success, setup mode ends by itself after 5 seconds and the page returns to
   the status view. On failure the reason is shown and the phone page can be
   used again.
5. Back ends setup mode. It also ends by itself after 10 minutes.

4.4.3 주변 Wi-Fi 검색 (scan + keyboard)

1. OK on 주변 Wi-Fi 검색. Nearby networks are listed, strongest first, with
   signal bars and a lock for password networks (up to 12; 6 visible at a
   time). The last row is 다시 검색 (Rescan).
2. ▲ / ▼ select, OK picks. An open network connects at once; otherwise the
   password keyboard opens.
3. Keyboard: ◀ ▶ ▲ ▼ move, OK types the selected key. Bottom row:
   ABC / #+= / abc (next page), 공백 (Space), ⌫ (delete), 연결 (Connect).
   Passwords need at least 8 characters ("8자 이상" is shown until then).
4. Back: keyboard → list → status view.

4.4.4 Behaviour

- The network is saved only after a successful connection. If a new network
  fails (3 tries), the RC goes back to the saved one.
- After a power cut or router restart the RC reconnects by itself (retry after
  1 s, 5 s, 10 s, then every 30 s).
- Once connected, the time is taken from the internet (NTP, Korean time) and
  written to the clock chip, so the clock is exact.
- The top bar and the settings header show the Wi-Fi icon and 연결됨 / 미연결.
- Wi-Fi runs in its own task; the screen and the PLC replies to MAIN are not
  blocked by it.

### 4.5 기기 설정

4.5.1 Select 기기 설정 (Engineer Mode) and press OK ("OK 버튼으로 진입").
The menu changes to ID 설정 and VSP 설정. Back returns to the normal menu.

4.5.2 ID 설정

| 표시 | 내용 |
|---|---|
| 현재 ID | Saved ID, e.g. "현재 ID : 98 (0x62)", or "00 (Default)" |
| [ - ]  ID [ N ]  [ + ] | New ID, changed with ◀ / ▶ or ▲ / ▼ |
| 설정 범위 | 1 ~ 99, OK = 저장 |

4.5.3 VSP 설정

| 표시 | 내용 |
|---|---|
| 제목 | Fan and group being set, e.g. "급기 - Supply Air [전열]" |
| 급기 카드 | [ 강 : 4.80 V ] [ 중 : 3.20 V ] [ 약 : 1.80 V ] |
| 배기 카드 | [ 강 : 4.60 V ] [ 중 : 3.00 V ] [ 약 : 1.60 V ] |

- ◀ / ▶ move through the 12 values in this order:
  [전열 배기] 약 → 중 → 강 → [전열 급기] 약 → 중 → 강 →
  [바이패스 배기] 약 → 중 → 강 → [바이패스 급기] 약 → 중 → 강
- ▲ / ▼ change the selected value by 0.05 V (0.55 V ~ 5.45 V).
- OK saves all values. Back cancels.

### 4.6 필터 상태

| 표시 | 내용 |
|---|---|
| 잔여율 | Ring with the remaining rate (%), "--%" before data arrives |
| 상태 칩 | 정상 / 청소 / 교체 |
| 누적 사용 | Used time (시간) |
| 잔여 시간 | Remaining time (시간) |
| 필터 초기화 (RESET) | "초기값 N 시간" |

4.6.0 Where the values come from: the main unit's D6 frame when it sends one.
The current main unit does not, so the RC counts the filter time itself
(`RC_FILTER_LOCAL_COUNT`): only while the unit is running
(`RC_FILTER_COUNT_RUNNING_ONLY`), saved every 10 minutes and kept after power
off. 잔여율 % and 잔여 시간 count down from 2,000 h; at 0 the chip and the top bar
show 필터 교체. 필터 초기화 sets the count back to 0.

4.6.1 The initial value is currently fixed at 2,000 시간 (`RC_FILTER_INIT_FIXED`),
so the filter alarm comes 2,000 hours after a reset. In the panel, OK resets the
filter time to 2,000 h; ◀ / ▶ do nothing. With `RC_FILTER_INIT_FIXED` set to 0,
◀ / ▶ change the initial value (100 ~ 9,900 시간, in 100-hour steps).

4.6.2 * 실내외 공기질 및 사용 환경에 따라 차이가 있을 수 있습니다.

### 4.7 언어 설정

| 표시 | 내용 |
|---|---|
| 화면 언어 (Display Language) | 한국어 (Korean) / English |

4.7.1 ◀ / ▶ select the language, OK saves it. All screens are rebuilt in the
new language right away, and the settings screen stays open.

4.7.2 한국어 shows the labels in Korean with English subtitles. English
shows English only.

4.7.3 The language is saved and kept after power off. Default: 한국어.

---

## 5. 표시 메시지

| 메시지 | 위치 | 의미 |
|---|---|---|
| 적용됨 - 다음 응답에 전송 (Queued) | 풍량 / 예약 상태 줄 | Accepted; sent to the main unit in the next reply |
| 무시됨 - LOCK/OFF/AUTO/취침/양압·음압 (Ignored) | 풍량 / 예약 상태 줄 | Not accepted (locked, power Off, auto mode, sleep mode, or a fan change in 양압 / 음압) |
| 양압 모드 시작 - 설정 저장됨 (Positive mode on) | 풍량 상태 줄 | Wizard applied; the mode started with the new setpoints (same for 음압) |
| 설정 저장됨 - 전원 Off/잠금이라 시작 안 함 (Saved - not started) | 풍량 상태 줄 | Wizard applied while the power was off or the unit was locked; setpoints saved only |
| 필요 풍량 … 더 큰 장치를 검토하세요. (Unit too small) | 마법사 결과 | The needed airflow is above the rated airflow; it runs capped |
| 음압 모드는 … 창문을 조금 열어 두세요. (Gas appliance) | 마법사 결과 (음압) | Shown until the first 음압 setup is applied, then never again |
| PLC 대기 (Idle) | 풍량 / 예약 상태 줄 | No command sent yet |
| 통신 끊김 (No link to main unit) | 풍량 / 예약 상태 줄 | No frame from the main unit for 5 seconds |
