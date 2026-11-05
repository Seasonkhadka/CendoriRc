#ifndef __RS485STRUCT_H__
#define __RS485STRUCT_H__

#include <cstdint>

/**
 * @brief RS-485 외부 프로토콜 (PC <-> MAIN) 프레임
 *        HEADER, Group, ID, D0~D5, CHECKSUM (HEADER ~ D5 XOR) = 10 바이트
 */
namespace RS485 {

	const uint8_t FRAME_LEN = 10;
	const uint8_t NO_CHANGE = 0xFE;		// 제어요청: 변화 없음
	const uint8_t TIMER_STOP = 0x63;	// 제어요청 D1/D2: 엘리먼트/필터 시간 멈춤

	enum Header {
	//	상태요청		제어요청		응답
		D5 = 0xD5,	E5 = 0xE5,	B5 = 0xB5,	// 일반 (모드/팬/시간/에러)
		D6 = 0xD6,				B6 = 0xB6,	// 룸콘 데이터1 (PM10, CO2, PM2.5, TVOC)
		D7 = 0xD7,	E7 = 0xE7,	B7 = 0xB7,	// 룸콘 데이터2 (온습도, 설정기능, 연동, 알람)
		D8 = 0xD8,				B8 = 0xB8,	// 통합센서 데이터1 (CRD-SEN)
		DB = 0xDB,				BB = 0xBB,	// 통합센서 데이터2 (CO, TVOC, PM1.0, 차압)
		DC = 0xDC,				BC = 0xBC,	// 룸콘 CO 센서
		D9 = 0xD9,				B9 = 0xB9,	// 팬 회전속도 (RPM)
		C6 = 0xC6,	C5 = 0xC5,	A5 = 0xA5,	// VSP 상태요청 / 제어요청 / 응답
	};

	// ---- D5/E5/B5 D0 : Mode & 팬속도 ----
	enum Mode_D0 {
		BYPASS			= 0x80,		// 0:전열 1:바이패스
		MODE_MASK		= 0x70,
		MODE_SHIFT		= 4,
		MODE_OFF		= 0,		// 전원 Off
		MODE_RUN		= 1,		// 환풍기 모드 (가동)
		MODE_RUN_RSV	= 2,		// 가동 예약 모드
		MODE_STOP_RSV	= 3,		// 정지 예약 모드 (문서에 없음 - PLC 와 같은 값)
		MODE_ELEM_RST	= 4,		// Element time reset
		MODE_FILTER_RST	= 5,		// Filter time reset
		AUTO			= 0x08,		// 0:수동 1:자동
		LOCK			= 0x04,		// 0:해제 1:설정
		FAN_MASK		= 0x03,		// 0:정지 1:약 2:중 3:강
	};

	// ---- B5 D5 : Error ----
	enum Err_B5 {
		NO_DEVICE		= 0x20,		// 장비 없음 (중앙제어 연동, 0:ON 1:OFF)
		COMM_ERR		= 0x10,		// 통신에러 (메인통신)
		FAN2_SUPPLY_ERR	= 0x02,		// FAN 2 (급기) 이상
		FAN1_EXHAUST_ERR= 0x01,		// FAN 1 (배기) 이상
	};

	// ---- D7/E7/B7 D3 : 설정기능 ----
	enum Func_D3 {
		HEATER_CTRL		= 0x80,		// 히터제어 0:비활성 1:활성
		HEATER_USED		= 0x40,		// 히터 0:미사용 1:사용 (응답만)
		CLEAN			= 0x10,		// 청정모드
		EXHAUST			= 0x08,		// 배기모드
		SUPPLY			= 0x04,		// 급기모드
		SLEEP			= 0x02,		// 취침
		CO2				= 0x01,		// CO2 (RC 에 없음 - 0)
	};

	// ---- B7 D4 : 연동기능 (제어 불가) ----
	enum Link_D4 {
		BATH			= 0x80,		// 욕실연동
		HOOD			= 0x40,		// 주방연동
	};

	// ---- B7 D5 : 알람 ----
	enum Alarm_B7 {
		FILTER_REPLACE	= 0x80,		// 필터교체 알람
		FILTER_CLEAN	= 0x40,		// 필터청소 알람
		FILTER_STOP		= 0x20,		// 필터알람으로 장비정지
		FIRE			= 0x10,		// 화재알람
	};

	typedef struct __attribute__((packed)) {
		uint8_t header = 0;		// 위 Header
		uint8_t group = 0;		// 그룹 ID (중앙제어기)
		uint8_t id = 0;			// 개별 ID (전열 장치)
		uint8_t d[6] = {};		// D0 ~ D5
		uint8_t checksum = 0;	// HEADER ~ D5 XOR
	} Frame;

	static_assert(sizeof(Frame) == FRAME_LEN, "RS485 frame must be 10 bytes");
}

#endif
