# CendoriRC 룸 콘트롤러 기능 설명서

ERV용 룸 콘트롤러 – Room controller for ERV main units

| 버전 | 작성일 | 변경내용 |
|---|---|---|
| 1.0 | 2026-10-23 | 최초작성 (First version) |

---

## 목 차

1. 사양
2. 동작 및 버튼설명
   - 2.1 초기동작
   - 2.2 전원버튼
   - 2.3 예약 설정
   - 2.4 모드 설정 (양압 / 음압 모드 2.4.8 ~ 2.4.10)
   - 2.5 필터 상태
   - 2.6 Up / Down 버튼
   - 2.7 에러코드
3. 관리자모드
   - 3.1 VSP 설정방법
   - 3.2 필터초기화
   - 3.3 필터초기값 설정
   - 3.4 히터사용여부 설정
   - 3.5 ID 설정
   - 3.6 실외 온도 확인

---

## 1. 사양

1.1 MCU: ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB PSRAM)

1.2 디스플레이: 5.0" TFT LCD, 800 × 480

1.3 통신방법: P.L.C (Power Line Communication), 2400 baud.
The main unit (MAIN) always speaks first; the room controller only answers.

1.4 터치버튼: 8 keys – 전원 (Power), 설정 (Setting), Back, OK, ▲ Up, ▼ Down,
◀ Left, ▶ Right

1.5 센서 사양

1.5.1 온습도 센서: SHT40
- Temperature range: -40 °C ~ 125 °C
- Humidity range: 0 ~ 100 %RH

1.5.2 CO2 센서: SCD40
- Photoacoustic NDIR
- Sampling interval: 5 s

1.5.3 초미세먼지 센서: PM2012
- Laser detection, PM1.0 / PM2.5 / PM10

1.5.4 VOC 센서: SGP40
- Output: TVOC ㎍/㎥, converted from the Sensirion VOC index (1 ~ 500, 100 = normal)
  with ppb = (ln(501 − index) − 6.24) × −381.97, then ㎍/㎥ = ppb × 4.5
  (index 100 ≈ 420 ㎍/㎥). Temperature / humidity compensation from the SHT40.
- No value ("--") for the first ~45 s after power-on (algorithm start-up).

1.5.5 CO 센서: electrochemical cell (analog input)
- ME2-CO cell, op-amp stage U13 (AD8572), SEN_VOUT on GPIO8.
- SEN_VOUT = VREF (455 mV) + I_cell × 142.9 kΩ (R53 + R54 ∥ RT1).
- ppm = (mV − zero) / (0.02 µA/ppm × 142.9 kΩ) ≈ (mV − zero) / 2.86.
  The cell sensitivity (0.02 µA/ppm) is not confirmed yet.
- Zero = lowest averaged voltage since power-on (auto zero). No value for the
  first 60 s (warm-up).

1.6 화면 구성

| No. | 표시 | 설명 |
|---|---|---|
| (1) | 상단 Wi-Fi 표시 | Wi-Fi icon and 연결됨 / 미연결 |
| (2) | 상단 상태 표시 | Error code (E1 / E2 / E3 / FAN ERR) or filter state (필터 정상 / 필터 청소 / 필터 교체) |
| (3) | 실외 온도 | "외기 N°C" in the top bar, from MAIN |
| (4) | 모드 배지 | Current mode on the dashboard (e.g. 전열교환 (HEAT RECOVERY), 자동 청정 (AUTO CLEAN), 정지 (OFF)) |
| (5) | 풍량 표시 | Fan level actually running (약 / 중 / 강) |
| (6) | 센서 값 | Temperature, humidity, CO2, PM2.5, PM10, VOC, CO (not connected yet) |

The two dashboards (A / B) are switched with ◀ / ▶.

1.7 설정 메뉴

| 메뉴 | 내용 |
|---|---|
| 풍량 설정 (Airflow) | Mode and fan level |
| 예약 설정 (Schedule) | 가동예약 / 정지예약 / 취침 / 취소 |
| 네트워크 설정 (Wi-Fi) | Wi-Fi connection |
| 기기 설정 (Engineer Mode) | ID 설정, VSP 설정 |
| 필터 상태 (Filter) | Filter remaining time, reset, initial value |

---

## 2. 동작 및 버튼설명

### 2.1 초기동작

2.1.1 When power is applied, the room controller loads the saved settings,
starts the sensors and shows the dashboard.

2.1.2 It then waits for MAIN. If no frame is received for 5 seconds,
"통신 끊김" is shown on the status line of 풍량 설정 and 예약 설정. Keys
still work, and the settings are sent once the link returns.

### 2.2 전원버튼

2.2.1 Press the 전원 key to turn the unit On / Off. The backlight follows.

### 2.3 예약 설정

2.3.1 Open 설정 → 예약 설정. In the "예약 종류" row, ◀ / ▶ cycle
[가동예약 → 정지예약 → 취침 → 취소].

2.3.2 In the "예약 시간" row, ◀ / ▶ set the reservation time in 10-minute
steps (shown as H:MM, maximum 24 hours).

2.3.3 Select "예약 전송 (Send)" and press OK to apply the reservation.

2.3.4 취소 cancels a waiting reservation and ends sleep mode.

2.3.5 취침모드는 항상 약풍으로 동작한다. The fan level cannot be changed
during sleep mode.

### 2.4 모드 설정

2.4.1 Open 설정 → 풍량 설정. In the mode row, ◀ / ▶ select
[전열 → 바이패스 → 청정 → 자동 → AI], and OK applies it. When 전열 or 바이패스 is
picked, a second row "전열 방식" / "바이패스 방식" chooses [일반 → 양압 → 음압]:
일반 is the normal balanced mode, 양압 / 음압 are the pressure modes (2.4.8) on
that air path. The mode can be changed
only while the power is On. Only one mode runs at a time.

2.4.2 전열 (전열) 모드: ventilates through the heat-exchange element to reduce
indoor energy loss.

2.4.3 바이패스 모드: ventilates without passing the heat-exchange element, to
reduce power use between seasons.

2.4.4 청정모드: circulates indoor air through the filter to clean it.

2.4.5 자동모드: the fan level and ventilation / clean are set automatically
from the CO2 sensor and the fine-dust sensor. The worse of the two is used.
A new result is applied at most once every 10 seconds.

2.4.6 AI 모드: separate from 자동모드.

2.4.7 센서상태 및 제어 테이블: Table 1), Table 2), Table 3), Table 4).

Table 1) VOC 센서 (index)

| 상태 | 값 |
|---|---|
| TVOC | ㎍/㎥ (from VOC index, index 100 ≈ 420 ㎍/㎥) |

The VOC value is displayed only and is not used in 자동모드.

Table 2) CO2 센서

| 상태 | 좋음 | 보통 | 나쁨 | 매우나쁨 |
|---|---|---|---|---|
| 값 | ~700 ppm | 701 ~ 1000 ppm | 1001 ~ 1500 ppm | 1501 ppm ~ |

Table 3) 초미세먼지 센서

| PM2.5 상태 | 좋음 | 보통 | 나쁨 | 매우나쁨 |
|---|---|---|---|---|
| PM2.5 값 | ~15 ㎍/㎥ | 16 ~ 35 ㎍/㎥ | 36 ~ 75 ㎍/㎥ | 76 ㎍/㎥ ~ |

| PM10 상태 | 좋음 | 보통 | 나쁨 | 매우나쁨 |
|---|---|---|---|---|
| PM10 값 | ~30 ㎍/㎥ | 31 ~ 80 ㎍/㎥ | 81 ~ 120 ㎍/㎥ | 121 ㎍/㎥ ~ |

The dust state is the worse of PM2.5 and PM10.

Table 4) 동작 상태 (자동모드)

| CO2 센서 | 초미세먼지 센서 | 풍량 | 모드 |
|---|---|---|---|
| 매우나쁨 | - | 강풍 | 전열 |
| 나쁨 | - | 중풍 | 전열 |
| 보통 | 매우나쁨 | 강풍 | 청정 |
| 보통 | 나쁨 | 중풍 | 청정 |
| 보통 | 보통 | 약풍 | 청정 |
| 보통 | 좋음 | 약풍 | 전열 |
| 좋음 | 매우나쁨 | 강풍 | 청정 |
| 좋음 | 나쁨 | 중풍 | 청정 |
| 좋음 | 보통 | 약풍 | 청정 |
| 좋음 | 좋음 | 정지 | 정지 |

When no sensor value is available, 자동모드 runs on 약풍, 전열 .
In auto clean, the dashboard mode badge shows "자동 청정 (AUTO CLEAN)".

2.4.8 양압 모드 (Positive mode) / 음압 모드 (Negative mode)

- 양압 모드: supply > exhaust. Keeps fine dust, odours and unfiltered outdoor
  air out of the room.
- 음압 모드: exhaust > supply. Keeps odours or contaminants inside the room.
- Both are standalone modes like 전열 / 바이패스 / 청정 / 자동. The existing
  four modes are unchanged and stay balanced.
- Air path: 전열 양압 / 음압 go through the heat-exchange element (bypass closed),
  바이패스 양압 / 음압 go through the bypass. 급기모드 / 배기모드 (single fan) are
  switched off, because both fans run. The saved setpoints are the same for both
  paths.
- Fan: each fan runs at its own saved setpoint. The fan level keys do not
  apply in these modes (the setpoint sets the airflow).
- The main unit has only one shared fan speed. The RC therefore sends it
  전열 or 바이패스 mode, plus a separate supply and exhaust VSP for the setpoint
  in that path's VSP set (C5 전열 1/2 or 바이패스 3/4). 약풍 = setpoint, 중풍 =
  CO2 boost.
- Leaving the mode (any other mode, or 급기모드 / 배기모드): the RC sends the
  normal VSP table back right away. The saved setpoints are kept.
- Power loss: after restart the RC resumes the last mode, including 양압 /
  음압, with the saved setpoints.
- 예약 (가동예약 / 정지예약) only turns the power on / off. The mode is kept, so a
  start reservation resumes 양압 / 음압 like any other mode.

2.4.9 설정 마법사 (Setup wizard)

The first time 양압 or 음압 is applied (under 전열 or 바이패스), the setup wizard
opens instead.
Nothing changes on the unit until 적용 (Apply) is pressed.

1. 공간 종류 (Room type)
2. 인원 (People) or 변기 수 (Toilets)
3. 결과 (Result), e.g. "급기 320 m³/h · 배기 295 m³/h · 양압 모드 +8%" → [적용]

Apply saves the setpoints and starts the mode. Later selections start the
mode directly with the saved setpoints. 풍량 설정 → 전열 or 바이패스 → "양압/음압
다시 설정 (Re-run pressure setup)" opens the wizard again.

Table 5) 공간 종류 (RcConfig.h `RC_PRESS_*`, values to confirm)

| 모드 | 공간 | 단위 풍량 | 차압 |
|---|---|---|---|
| 양압 | 개방형 사무실 (Open office) | 32 m³/h per person | +8% |
| 양압 | 회의실 (Meeting room) | 32 m³/h per person | +5% |
| 양압 | 개인 사무실 (Private office) | 32 m³/h per person | +5% |
| 음압 | 화장실 (Restroom) | 90 m³/h per toilet (exhaust) | −10% |
| 음압 | 기타 공간 (Other room) | 32 m³/h per person | −10% |

Calculation:

- Larger fan (supply in 양압, exhaust in 음압) = count × unit airflow, at least
  50 m³/h, rounded up to 5 m³/h, at most the rated airflow
  (`RC_FAN_RATED_M3H`).
- Smaller fan = larger fan ÷ (1 + offset), rounded down to 5 m³/h, and at
  least 5 m³/h below the larger fan. The VSP also keeps at least one 0.05 V
  step of difference, so the direction of the offset never flips.
- The airflow → VSP conversion is linear between `RC_FAN_VSP_AT_ZERO` and
  `RC_FAN_VSP_AT_RATED` at the rated airflow (fan curve not confirmed).

2.4.10 CO2 부스트 (only with the RC's CO2 sensor)

- CO2 > 1000 ppm for 5 minutes → both fans +20%, same supply / exhaust ratio
  (main unit fan level 중풍).
- CO2 < 800 ppm for 10 minutes → back to the saved setpoints (약풍).
- The rated airflow is never exceeded. If +20% would exceed it, both fans are
  scaled down together.
- While the boost runs, the screens show "CO₂ 부스트 (CO₂ boost)".

Warnings (one sentence each: what is happening and what to do):

| 경우 | 메시지 |
|---|---|
| Needed airflow > rated | 필요 풍량 400 m³/h가 정격 350 m³/h보다 커서 350 m³/h로 제한되니, 더 큰 장치를 검토하세요. / Unit too small: it needs 400 m³/h but is rated 350 m³/h, so it runs at 350 m³/h - consider a larger unit. |
| First 음압 setup (shown once) | 음압 모드는 가스기기의 배기가스를 실내로 끌어들일 수 있으니, 가스기기를 쓸 때는 창문을 조금 열어 두세요. / Negative mode can pull fumes back from gas appliances, so open a window slightly while one is in use. |

Table 6) 확인 결과 (expected)

| 경우 | 급기 | 배기 | 결과 |
|---|---|---|---|
| 양압, 개방형 사무실, 10명 | 320 | 295 | +8% |
| 양압, 회의실, 12명 | 385 | 365 | +5% |
| 양압, 개인 사무실, 1명 | 50 (최소) | 45 | +5% |
| 음압, 화장실, 변기 2개 | 160 | 180 | −10% |
| 양압 중 CO2 1100 ppm 5분 | +20% | +20% | 비율 유지 |
| 양압 → 자동 선택 | 자동 | 자동 | 원래 VSP, 자동 동작 |
| 자동 → 양압 (설정 완료) | 320 | 295 | 저장값 사용 |
| 음압 첫 선택 (설정 없음) | — | — | 마법사 열림 |
| 음압 설정 결과 | — | — | 가스기기 경고 한 번 |
| 양압 중 정전 후 재시작 | 320 | 295 | 양압 재개 |
| 필요 400, 정격 350 | 350 | — | "장치 용량 부족" 경고 |

The first four rows and the 400 / 350 row were checked with a PC test of
the calculation. The other rows need a check on the unit.

### 2.5 필터 상태

2.5.1 Open 설정 → 필터 상태 to see the filter remaining rate (%), the used
time and the remaining time.

2.5.2 The state chip shows 정상, 청소 or 교체.

2.5.3 The top bar shows 필터 청소 or 필터 교체 when MAIN reports it.

### 2.6 Up / Down 버튼

2.6.1 In 설정 → 풍량 설정, the fan row sets the fan level with ◀ / ▶ and OK.
The fan level can be set only in manual mode (not in 자동모드 or 취침모드).

| 약풍 | 중풍 | 강풍 |
|---|---|---|

2.6.2 In the setting screens, ▲ / ▼ move between items, ◀ / ▶ change the
value, OK saves or sends, Back cancels.

### 2.7 에러코드

| 번호 | 에러 | 에러코드 | 에러 설명 |
|---|---|---|---|
| 1 | 급기 팬 에러 | E1 | The supply fan does not run or no TACHO signal is detected |
| 2 | 배기 팬 에러 | E2 | The exhaust fan does not run or no TACHO signal is detected |
| 3 | 급기 팬/배기 팬 동시에러 | E3 | Both fans do not run or no TACHO signal is detected |
| 4 | 외기 팬 에러 | FAN ERR | OA fan error reported by MAIN |

---

## 3. 관리자모드

Open 설정 → 기기 설정 (Engineer Mode) and press OK. The menu shows
ID 설정 and VSP 설정.

### 3.1 VSP 설정방법

3.1.1 Select 기기 설정 → VSP 설정.

3.1.2 ◀ / ▶ move through the setting order. The label shows [전열] or
[바이패스].

3.1.3 설정순서

[전열 배기] 약 → 중 → 강 → [전열 급기] 약 → 중 → 강 →
[바이패스 배기] 약 → 중 → 강 → [바이패스 급기] 약 → 중 → 강

3.1.4 ▲ or ▼ increases or decreases the value by 0.05 V
(range 0.55 V ~ 5.45 V).

3.1.5 OK saves the settings and sends them to MAIN.

3.1.6 기본값

| | 약 | 중 | 강 |
|---|---|---|---|
| 배기 (전열 / 바이패스) | 1.60 V | 3.00 V | 4.60 V |
| 급기 (전열 / 바이패스) | 1.80 V | 3.20 V | 4.80 V |

### 3.2 필터초기화

In 설정 → 필터 상태, select "필터 초기화 (RESET)" and press OK. The filter time
is reset to the initial value, so the filter alarm comes 2,000 hours later.
시간은 표기값 × 100 한 값, 예) 초기값 20 이면 2,000 시간이다.

### 3.3 필터초기값 설정

The initial value is currently fixed at 20 (2,000 시간). ◀ / ▶ do not change it,
and values from the main unit or older saved settings are ignored.
When this is enabled again (`RC_FILTER_INIT_FIXED` = 0): ◀ / ▶ set the initial
value (1 ~ 99), and OK saves it. 시간은 표기값 × 100 한 값. (기본값 20)

### 3.4 히터사용여부 설정

On the dashboard, hold ▲ + ▼ for 3 seconds to turn the heater function On.
Hold them again for 3 seconds to turn it Off.

### 3.5 ID 설정

3.5.1 Sets the ID used by the home network, the central controller and the
PC control program.

3.5.2 Select 기기 설정 → ID 설정. ◀ / ▶ change the ID (1 ~ 99), and OK saves it.

### 3.6 실외 온도 확인

3.6.1 The outdoor temperature is always shown in the top bar while MAIN is
connected.

3.6.2 The outdoor temperature is available only on models with an outdoor
temperature sensor.
