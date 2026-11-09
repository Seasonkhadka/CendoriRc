#ifndef __RS485CONFIG_H__
#define __RS485CONFIG_H__

/**
 * @brief RS-485 외부 프로토콜 (PC <-> 이 기기) 설정값
 *        문서: ERV 외부 통신 프로토콜 (센도리) Ver 1.2. 이 기기가 MAIN 역할 (PC 가 Polling)
 *        확인 안 된 항목은 모두 여기에 둔다.
 */

// ---- 하드웨어 (CD-WD-RMC-V1.0: U10 THVD1400, DE/RE 묶음) ----
#define RS485_UART_NUM				1		// UART1
#define RS485_PIN_TX				17		// 485-TX -> U10 D
#define RS485_PIN_RX				18		// 485-RX <- U10 R
#define RS485_PIN_DE				48		// 485-TE -> U10 DE/RE (HIGH = 송신)
#define RS485_BAUD					9600	// 8N1

// ---- 주소 ----
#define RS485_GROUP					0x00	// 중앙제어기 그룹 ID (문서 0x00~0xFE). RC 에 설정 화면 없음
#define RS485_ID_DOC_MAX			0x20	// 문서상 개별 ID 최대 (1~32) - 화면 안내용
// 개별 ID = 기기 설정 -> ID 설정 (1~99). 미설정(0) 이면 RS-485 송수신 없음.

// ---- 타이밍 ----
#define RS485_FRAME_GAP_MS			20		// 바이트 사이 이 시간 이상 비면 프레임 버림 (1바이트 = 1.04ms)
#define RS485_REPLY_DELAY_MS		5		// 요청 끝 -> 응답 시작 (문서에 없음)
#define RS485_AUTO_B6_MS			1000	// 룸콘 데이터(B6) 자동 송신 주기. 0 = 끄고 D6 폴링만. 버스 유휴 시에만 송신

// ---- 데이터 해석 (문서 확인 필요) ----
#define RS485_VSP_X10				1		// 1: VSP = 전압 x10 (문서 예제: 2.3V -> 23). 0: 상위니블 V, 하위니블 0.1V (문서 본문)
#define RS485_FILTER_UNIT_H			100		// B5 D2 필터시간 단위 (RC 와 같이 100시간 단위, 잔여시간 올림)
#define RS485_ELEMENT_TIME			0		// B5 D1 엘리먼트 시간 - RC 에 없음
#define RS485_RESERVE_UNIT_MIN		60		// D3/D4 예약시간 단위 (문서: 시간)
#define RS485_LINK_ERR_FROM_PLC		1		// 1: B5 D5 bit4(통신에러) = PLC MAIN 링크 끊김

// ---- 제어 허용 ----
#define RS485_ALLOW_CONTROL			1		// 1: E5 / E7 / C5 로 PC 가 상태 변경 가능
#define RS485_LOG_FRAMES			1		// 1: 송수신 프레임 로그

#endif
