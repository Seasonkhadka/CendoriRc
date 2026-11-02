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
   - 2.4 모드 설정
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
[환기 → 바이패스 → 청정 → 자동 → AI], and OK applies it. The mode can be
changed only while the power is On.

2.4.2 환기 (전열) 모드: ventilates through the heat-exchange element to reduce
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
| 매우나쁨 | - | 강풍 | 환기 |
| 나쁨 | - | 중풍 | 환기 |
| 보통 | 매우나쁨 | 강풍 | 청정 |
| 보통 | 나쁨 | 중풍 | 청정 |
| 보통 | 보통 | 약풍 | 청정 |
| 보통 | 좋음 | 약풍 | 환기 |
| 좋음 | 매우나쁨 | 강풍 | 청정 |
| 좋음 | 나쁨 | 중풍 | 청정 |
| 좋음 | 보통 | 약풍 | 청정 |
| 좋음 | 좋음 | 정지 | 정지 |

When no sensor value is available, 자동모드 runs on 약풍, 환기.
In auto clean, the dashboard mode badge shows "자동 청정 (AUTO CLEAN)".

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
