/*
 * PLC internal protocol (메인 ↔ R/C) frame layouts, per the protocol
 * provider's own reference pattern. #pragma once used here instead of the
 * provider's __PLCSTRUCT_H__ guard, matching this project's other headers -
 * everything else (namespace, enum names/values, struct layouts, comments)
 * is kept as given.
 */
#pragma once

#include <cstdint>

namespace PLC {

    const uint8_t NO_CHANGE = 0xFE;    // E5 Data1/Data2/Data3: 변화 없을 때
    const uint8_t FRAME_LEN = 6;    // Header + Data1~4 + Checksum
    const uint8_t DEVICE_ID = 0x00;    // B5 Data1 기본값

    enum Header {
//        상태요청        제어요청    응답
        D5 = 0xD5,    E5 = 0xE5,    B5 = 0xB5,    // 상태요청1 / 제어요청 / 응답
        D6 = 0xD6,            B6 = 0xB6,    // 상태요청2 / 응답 (SHR-L1/L2)
        D7 = 0xD7,    E7 = 0xE7,    B7 = 0xB7,    // 상태요청3 / 제어요청 / 응답 (SHR-L1/L2)
        D8 = 0xD8,            B8 = 0xB8,    // 센서값 통보 / 응답 (CO, SHR-L1 only)
        D9 = 0xD9,                    // 센서값 통보 (TVOC/온습도)
        A6 = 0xA6,                    // VSP 제어 (MAIN -> RC)
        C5 = 0xC5,                    // VSP 변경 (RC -> MAIN, 룸콘 KEY 조작)
    };

// ---- D5 (상태요청1) : Data4 에러 비트 ----
    enum Err_D5 {
        FILTER_REPLACE_ALARM = 0x80,    // 필터교체알람 (필터압력체크)
        FILTER_CLEAN_ALARM   = 0x40,    // 필터청소알람 (필터압력체크)
        FILTER_ALARM_STOP    = 0x20,    // 필터알람으로 장비정지
        RESV_ALARM           = 0x10,    // 예비(알람)
        RESV_ERR             = 0x08,    // 예비(에러)
        OA_FAN_ERR           = 0x04,    // 제연/OA 팬 에러
        EXHAUST_FAN_ERR      = 0x02,    // 배기 팬 에러
        SUPPLY_FAN_ERR       = 0x01,    // 급기 팬 에러
    };

// ---- E5 (제어요청) : Data1 비트 (Mode & 팬속도 & 부가기능) ----
    enum Mode_E5_Data1 {
        BYPASS_E5      = 0x80,    // 0:전열모드 1:바이패스
        MODE_MASK_E5   = 0x70,    // AND 연산으로 추출
        MODE_STOP      = 0x00,    // 정지모드
        MODE_RUN       = 0x10,    // 가동모드
        MODE_RUN_RSV   = 0x20,    // 가동예약 모드
        MODE_STOP_RSV  = 0x30,    // 정지예약 모드
        MODE_UNUSED    = 0x40,    // 미처리
        MODE_FILTER_RST= 0x50,    // Filter Time reset
        AUTO_E5        = 0x08,    // 0:수동모드 1:자동모드
        LOCK_E5        = 0x04,    // 0:해제 1:설정
        FAN_MASK_E5    = 0x03,    // 팬속도 0~3 (0:정지 1:약풍 2:중풍 3:강풍)
    };

    // E5 Data2 (Time mode) : 변화없음=0xFE, 값 1,2,3,4,10 을 순번 반복송신
    enum TimeMode_E5_Data2 {
        TM_UNUSED       = 1,    // 미처리
        TM_FILTER_SET   = 2,    // Filter Time set - 100시간 단위
        TM_RUN_RSV_SET  = 3,    // 가동예약 Time set - 시간단위, MAX 24
        TM_STOP_RSV_SET = 4,    // 정지예약 Time set - 시간단위, MAX 24
        TM_FUNCTION     = 10,    // Function 추가 (Data3 해석 전환)
    };

    // E5 Data3, Time mode==10(Function) 일 때의 기능 비트
    enum Function_E5_Data3 {
        FIRE_E5        = 0x80,    // 화재알람
        HEAT_E5        = 0x40,    // 0:해제 1:설정
        DUST_REMOVE_E5 = 0x20,    // 먼지제거 0:해제 1:설정
        CLEAN_E5       = 0x10,    // 청정모드 0:해제 1:설정
        HEATER_RUN_E5  = 0x08,    // 히터 가동상태 0:미가동 1:가동
        SLEEP_E5       = 0x04,    // 취침모드 0:해제 1:설정
        BATH_E5        = 0x02,    // 욕실연동
        HOOD_E5        = 0x01,    // 주방연동
    };

    typedef struct {
        uint8_t header = 0;        // D5: 상태요청1, E5: 제어요청, B5: 응답
        uint8_t data1 = 0;        // D5:외기온도+20 / E5:Mode&팬&부가기능 / B5:Device ID
        uint8_t data2 = 0;        // D5:CO2/100 / E5:TimeMode / B5:Mode&TimeSet(0~8)
        uint8_t data3 = 0;        // D5:CO2%100 / E5:TimeSet or Function / B5:Bypass,Heater,제어FLAG,Clean,Auto,Lock,팬속도
        uint8_t data4 = 0;        // D5:Error / E5:예비(에러) / B5:Time or Function2(TimeSet==8일때)
        uint8_t checksum = 0;    // Header~Data4 합산 후 2의 보수
    } d5e5b5;

// ---- B5 (응답) : Data3 비트 ----
    enum Mode_B5_Data3 {
        BYPASS_B5      = 0x80,    // 0:전열모드 1:바이패스
        HEATER_B5      = 0x40,    // 0:미사용 1:사용
        CTRL_FLAG_B5   = 0x20,    // 0:상태요청응답, 1:RC발 제어요청  <-- 핵심
        CLEAN_B5       = 0x10,    // 청정모드 0:해제 1:설정
        AUTO_B5        = 0x08,    // 0:수동모드 1:자동모드
        LOCK_B5        = 0x04,    // 0:해지 1:설정
        FAN_MASK_B5    = 0x03,    // 팬속도 0~3
    };

    // B5 Data2 (Mode & Time set)
    enum Mode_B5_Data2 {
        B5_STOP        = 0,    // 정지모드
        B5_RUN         = 1,    // 가동모드
        B5_RUN_RSV     = 2,    // 가동예약 모드
        B5_STOP_RSV    = 3,    // 정지예약 모드
        // 4 : -
        B5_FILTER_TIME = 5,    // 필터 시간
        B5_RUN_RSV_TM  = 6,    // 가동예약 시간
        B5_STOP_RSV_TM = 7,    // 정지예약 시간
        B5_FUNCTION    = 8,    // Function 설정 (Data4 해석 전환)
    };

    // B5 Data4, Mode(Data2)==8 일 때의 Function2 비트
    enum Function2_B5_Data4 {
        EXHAUST_B5      = 0x80,    // 배기모드 0:해제 1:설정
        SUPPLY_B5        = 0x40,    // 급기모드 0:해제 1:설정
        DUST_REMOVE_B5   = 0x20,    // 먼지제거 0:해제 1:설정
        // bit4,3 예비
        SLEEP_B5         = 0x04,    // 취침 0:해제 1:설정
        // bit1 예비
        CO2_AUTO_CLEAN_B5= 0x01,    // 0:자동전열  1:자동청정 (SHR-L1 전용)
    };

// ---- D6 (상태요청2) : Data1 비트 ----
    enum Mode_D6_Data1 {
        HEATER_RUN_D6 = 0x08,    // 히터 가동상태: 0:미가동 1:가동중 (Bit3, 나머지 예비)
    };

    typedef struct {
        uint8_t header = 0;        // D6: 상태요청2, B6: 응답(SHR-L1/L2)
        uint8_t data1 = 0;        // D6:히터가동상태(bit3) / B6:CO2/20 (0~5000ppm)
        uint8_t data2 = 0;        // D6:필터잔여시간(100h단위) / B6:PM2.5 (0~255)
        uint8_t data3 = 0;        // D6:필터사용% 상위 / B6:TVOC 상위바이트
        uint8_t data4 = 0;        // D6:필터사용% 하위 / B6:TVOC 하위바이트
        uint8_t checksum = 0;
    } d6b6;

// ---- D7/E7/B7 : 제어/상태 비트 (Data3) ----
    enum Mode_D7E7_Data3 {
        HEATER_CTRL_D7 = 0x40,    // 히터제어 0:해제 1:설정
        CLEAN_D7       = 0x10,    // 청정모드 0:해제 1:설정
        EXHAUST_D7     = 0x08,    // 배기모드 0:해제 1:설정
        SUPPLY_D7      = 0x04,    // 급기모드 0:해제 1:설정
        SLEEP_D7       = 0x02,    // 취침 0:해제 1:설정
        AUTO_D7        = 0x01,    // 자동 0:해제 1:설정
    };

    // B7 Data3 (SHR-L1/L2 응답) 비트
    enum Mode_B7_Data3 {
        FILTER_ALARM_B7 = 0x20,    // 0:정상 1:알람
        EXHAUST_B7      = 0x08,
        SUPPLY_B7       = 0x04,
        SLEEP_B7        = 0x02,
    };

    typedef struct {
        uint8_t header = 0;        // D7:상태요청3, E7:제어요청, B7:응답(SHR-L1/L2)
        uint8_t data1 = 0;        // D7/E7: 예비(0x00) / B7: 실내온도+20
        uint8_t data2 = 0;        // D7/E7: 예비(0x00) / B7: PM10 (0~255)
        uint8_t data3 = 0;        // D7:상태(0x00) E7:제어비트 / B7:필터알람,배기,급기,취침 비트
        uint8_t data4 = 0;        // D7/E7: 예비(0x00) / B7: 습도(0~99%)
        uint8_t checksum = 0;
    } d7e7b7;

// ---- D8 (센서값 통보, MAIN -> RC) ----
    typedef struct {
        uint8_t header = D8;
        uint8_t data1 = 0;    // 내부온도 -127~+127℃ (b7:0양수/1음수, b6~b0:데이터)
        uint8_t data2 = 0;    // 습도 0~100%
        uint8_t data3 = 0;    // PM2.5
        uint8_t data4 = 0;    // PM10
        uint8_t checksum = 0;
    } d8;

// ---- B8 (SHR-L1 전용 응답, CO 센서) ----
    typedef struct {
        uint8_t header = B8;
        uint8_t data1 = 0;    // CO 센서 상위바이트
        uint8_t data2 = 0;    // CO 센서 하위바이트
        uint8_t data3 = 0;    // 예비(0x00)
        uint8_t data4 = 0;    // 예비(0x00)
        uint8_t checksum = 0;
    } b8;

// ---- D9 (센서값 통보, MAIN -> RC) ----
    typedef struct {
        uint8_t header = D9;
        uint8_t data1 = 0;    // TVOC 상위바이트
        uint8_t data2 = 0;    // TVOC 하위바이트
        uint8_t data3 = 0;    // 온도+20
        uint8_t data4 = 0;    // 습도
        uint8_t checksum = 0;
    } d9;

// ---- A6 (VSP 제어, MAIN -> RC) ----
    enum Mode_A6_Data1 {
        VSP_EXHAUST_HEAT_EXCH = 1,    // VSP 배기 전열모드
        VSP_SUPPLY_HEAT_EXCH  = 2,    // VSP 급기 전열모드
        VSP_EXHAUST_BYPASS    = 5,    // VSP 배기 바이패스
        VSP_SUPPLY_BYPASS     = 6,    // VSP 급기 바이패스
    };

    typedef struct {
        uint8_t header = A6;
        uint8_t data1 = 0;    // Mode VSP (위 enum 참조)
        uint8_t data2 = 0;    // 약풍값 x2 (5x2 < VSP < 55x2), 예:2.4V -> 0x30
        uint8_t data3 = 0;    // 중풍값 x2, 예:3.0V -> 0x3C
        uint8_t data4 = 0;    // 강풍값 x2, 예:4.5V -> 0x5A
        uint8_t checksum = 0;
    } a6;

// ---- C5 (VSP 변경 통보, RC -> MAIN, 룸콘 KEY 조작) ----
    enum Mode_C5_Data1 {
        C5_VSP_EXHAUST_HEAT_EXCH = 1,    // VSP 배기 전열모드
        C5_VSP_SUPPLY_HEAT_EXCH  = 2,    // VSP 급기 전열모드
        C5_VSP_EXHAUST_BYPASS    = 3,    // VSP 배기 바이패스
        C5_VSP_SUPPLY_BYPASS     = 4,    // VSP 급기 바이패스
    };

    typedef struct {
        uint8_t header = C5;
        uint8_t data1 = 0;    // Mode VSP (위 enum 참조)
        uint8_t data2 = 0;    // 약풍값 x2
        uint8_t data3 = 0;    // 중풍값 x2
        uint8_t data4 = 0;    // 강풍값 x2
        uint8_t checksum = 0;
    } c5;

    static_assert(sizeof(d5e5b5) == FRAME_LEN, "PLC frame must be 6 bytes");
    static_assert(sizeof(d6b6) == FRAME_LEN, "PLC frame must be 6 bytes");
    static_assert(sizeof(d7e7b7) == FRAME_LEN, "PLC frame must be 6 bytes");
    static_assert(sizeof(b8) == FRAME_LEN, "PLC frame must be 6 bytes");
    static_assert(sizeof(c5) == FRAME_LEN, "PLC frame must be 6 bytes");

}
