#ifndef __RCCONFIG_H__
#define __RCCONFIG_H__

/**
 * @brief RC 동작 설정 - 프로토콜 문서로 확인되지 않은 항목은 모두 여기서 전환
 *
 * 1 = 사용, 0 = 미사용. 라인에서 확인되면 값만 바꾸고 코드는 그대로 둔다.
 */

// ---- 제어 FLAG (B5 Data3 bit5) ----
#define RC_CTRL_FLAG_ON_LOCAL_CHANGE	1	// UI 변경 후 첫 D5 응답(B5)에 제어 FLAG 설정, 전송 후 해제
#define RC_CTRL_FLAG_IN_E5_REPLY		0	// E5 응답(B5)에도 대기 중인 제어 FLAG 포함

// ---- 대기 프레임 (D5 응답 자리에 대신 전송) ----
#define RC_SEND_C5_ON_VSP_CHANGE		1	// VSP 변경 시 다음 B5 대신 C5 전송 (팬 1개당 1프레임)
#define RC_SEND_RESERVE_TIME_FRAME		1	// 예약 설정 시 B5 Data2 = 6/7, Data4 = 시간 프레임 전송
#define RC_RESERVE_TIME_AFTER_MODE		1	// 1: B5 Data2 = 2/3 전송 후 시간 프레임, 0: 시간 프레임 먼저
#define RC_RESERVE_TIME_CTRL_FLAG		1	// 시간 프레임 Data3 에 제어 FLAG 설정
#define RC_SEND_FUNCTION_FRAME			1	// 배기/급기/취침/먼지제거 변경 시 B5 Data2 = 8 프레임 전송
#define RC_FUNCTION_FRAME_CTRL_FLAG		1	// Function 프레임 Data3 에 제어 FLAG 설정

// ---- 상태요청 2/3 응답 ----
#define RC_REPLY_TO_D6					1	// D6 에 응답
#define RC_D6_REPLY_HEADER				PLC::B6	// D6 응답 헤더 (B6 또는 B8)
#define RC_REPLY_TO_D7					1	// D7 / E7 에 B7 응답

// ---- 수신값 해석 ----
#define RC_FILTER_USAGE(hi, lo)			((uint16_t)(((hi) << 8) | (lo)))	// D6 Data3/4 필터 사용% 조합 방식
#define RC_B7_FILTER_ALARM_MASK			(PLC::FILTER_REPLACE_ALARM | PLC::FILTER_CLEAN_ALARM)	// D5 에러 중 B7 필터알람으로 보낼 비트

// ---- 모드 / 풍량 ----
#define E5_FAN0_KEEPS_FAN				1	// E5 풍량 비트 0 = 기존 풍량 유지
#define AI_SETS_AUTO_BIT				0	// AI 모드(RC 전용)에서 B5 자동 비트 설정 여부

// ---- 기기 ID ----
#define DEFAULT_DEVICE_ID				0x00	// 사용자 선택 전 B5 Data1 (프로토콜 기본값, 라인 확인값)
#define DEVICE_ID_MIN					1
#define DEVICE_ID_MAX					99

// ---- 예약 (구 RC: 10분 단위, 가동예약 -> 정지예약 -> 취침 -> 취소) ----
#define RC_RESERVE_STEP_MIN				10		// UI 예약시간 단위 (분)
#define RC_RESERVE_MAX_MIN				1440	// 최대 24시간
#define RC_RESERVE_TX_UNIT_MIN			60		// B5 Data4 / E5 Data3 예약시간 1 = 이 분 (프로토콜 문서: 시간단위). 10 이면 10분 단위
#define RC_RESERVE_TX_MAX				(RC_RESERVE_MAX_MIN / RC_RESERVE_TX_UNIT_MIN)
#define RC_SLEEP_FAN					1		// 취침모드는 항상 약풍

// ---- 자동 모드 (RC 가 CO2 / 미세먼지로 풍량, 환기/청정 결정 - 구 RC Table 4) ----
#define RC_AUTO_MIN_HOLD_MS				10000	// 자동 결과 변경 최소 간격 (깜빡임 방지)
#define RC_AUTO_NO_SENSOR_FAN			1		// CO2, 미세먼지 둘 다 값 없을 때 풍량 (환기)
#define RC_AUTO_PM_USES_PM10			1		// 미세먼지 상태 = PM2.5 / PM10 중 나쁜 값
#define RC_AUTO_CLEAN_SETS_CLEAN_BIT	1		// 자동 청정이면 B5 청정 비트 설정

// ---- 실내 종합 공기질 지수 (AirIndex, 0~500, 최악 항목) ----
#define RC_AQI_USE_TVOC					1		// TVOC ug/m3 (VOC Index 환산) 를 mg/m3 표에 넣음
#define RC_AQI_USE_CO					1		// CO ppm (ME2-CO 환산, 감도 미확인)

// ---- 필터 (B5 Data2 = 5, Data4 = 초기값 x100h) ----
#define RC_SEND_FILTER_FRAME			1		// 필터 초기화 / 초기값 설정 시 B5 Data2 = 5 전송
#define RC_FILTER_INIT_DEFAULT_X100		20		// 필터 초기값 기본 (20 = 2,000시간)
#define RC_FILTER_INIT_FIXED			1		// 1: 초기값을 기본값(2,000시간)으로 고정 - 저장값/MAIN 값/UI 변경 무시
												//    필터 초기화 후 2,000시간에 알람. 0 이면 1~99 변경 가능
// MAIN 이 D6 (필터 잔여시간) 를 보내지 않으므로 RC 가 직접 사용시간을 센다.
// D6 를 받으면 MAIN 값이 우선. 리셋(필터 초기화) 시 0 으로.
#define RC_FILTER_LOCAL_COUNT			1		// 1: RC 자체 필터 사용시간 계산 / 표시 / 교체 알람
#define RC_FILTER_COUNT_RUNNING_ONLY	1		// 1: 가동 중일 때만 셈, 0: RC 전원 켜진 시간 전부
#define RC_FILTER_SAVE_EVERY_MIN		10		// 사용시간 NVS 저장 주기 (분) - 정전 시 최대 이만큼 손실
#define RC_FILTER_INIT_MIN				1
#define RC_FILTER_INIT_MAX				99

// ---- 센서 응답 값 ----
#define RC_B6_CO2_DIV					20	// B6 Data1 = CO2 / 20

// ---- 기기 설정 PIN (4자리, 0000~9999) ----
// Factory default until the user changes it in Engineer Mode (NVS "ui" / "eng_pin").
#define RC_ENGINEER_PIN					0		// 0000

// ---- 링크 / 저장 ----
#define RC_LINK_TIMEOUT_MS				5000	// 마지막 유효 프레임 후 이 시간 지나면 "메인 연결 끊김"
#define RC_NVS_SAVE_DELAY_MS			2000	// 마지막 변경 후 NVS 저장까지 대기

#endif
