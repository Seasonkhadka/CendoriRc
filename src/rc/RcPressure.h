#ifndef __RCPRESSURE_H__
#define __RCPRESSURE_H__

#include <cstdint>

#include "RcConfig.h"

/**
 * @brief 양압 / 음압 모드 (RC 전용, 프로토콜에 없음) - 풍량 계산
 *
 * 설정 마법사: 공간 종류 -> 인원 (또는 변기 수) -> 결과 (급기 / 배기 m3/h, 차압 %).
 * MAIN 에는 전열 모드 + 급기 / 배기 따로 다른 전열 VSP (C5) 로 전달된다
 * (RcController). 계산식 / 표 값은 RcConfig.h 의 RC_PRESS_* 로 조정.
 *
 * 하드웨어 / LVGL 의존 없음 (호스트 테스트 가능).
 */
namespace RC {

	enum PressureKind {
		PRESS_NONE = 0, PRESS_POSITIVE = 1, PRESS_NEGATIVE = 2
	};

	enum CountKind {
		COUNT_PEOPLE = 0, COUNT_TOILETS = 1
	};

	// 마법사 공간 종류 한 줄
	typedef struct {
		const char* ko;
		const char* en;
		uint8_t countKind;		// CountKind
		uint16_t perUnit;		// m3/h (1인당 또는 변기 1개당) - 큰 쪽 팬의 필요 풍량
		uint8_t offsetPct;		// 차압 % (큰 쪽 팬 / 작은 쪽 팬 - 1)
		uint8_t defCount;		// 마법사 기본값
		uint8_t maxCount;
	} PressureRoom;

	/**
	 * @brief 저장된 설정점 (양압 / 음압 각각, NVS 저장)
	 */
	typedef struct {
		bool valid;			// 마법사 적용 완료
		uint8_t room;			// pressureRooms() 인덱스
		uint8_t count;			// 인원 / 변기 수
		uint16_t supply;		// 급기 m3/h
		uint16_t exhaust;		// 배기 m3/h
		uint8_t offsetPct;		// 목표 차압 % (부호는 kind: 양압 +, 음압 -)
		uint16_t needed;		// 계산 필요 풍량 (정격 제한 전, 큰 쪽 팬)
		bool capped;			// 필요 풍량 > 정격 -> 정격으로 제한 ("장치 용량 부족")
	} PressureSetup;

	/**
	 * @brief 공간 종류 표 (kind 별). n 에 개수
	 */
	const PressureRoom* pressureRooms(uint8_t kind, int& n);

	/**
	 * @brief 설정점 계산
	 *  큰 쪽 팬 = max(인원 x 단위풍량, 최소풍량) 을 5 단위 올림, 정격 제한
	 *  작은 쪽 팬 = 큰 쪽 / (1 + 차압%) 을 5 단위 내림, 큰 쪽보다 최소 5 작게
	 *  양압: 급기 = 큰 쪽, 음압: 배기 = 큰 쪽
	 */
	PressureSetup pressureCompute(uint8_t kind, uint8_t room, uint8_t count,
								uint16_t ratedM3h = RC_FAN_RATED_M3H);

	/**
	 * @brief CO2 부스트 풍량: 두 팬 모두 +RC_PRESS_BOOST_PCT %, 비율 유지, 정격 초과 안 함
	 */
	void pressureBoost(uint16_t& supply, uint16_t& exhaust, uint16_t ratedM3h = RC_FAN_RATED_M3H);

	/**
	 * @brief 풍량 -> VSP (0.05V 단위 = PLC 값). RC_FAN_VSP_* 선형 (미확인), 유효 범위로 제한
	 */
	uint8_t airflowToVsp(uint16_t m3h);

	/**
	 * @brief 설정점 -> 급기 / 배기 VSP. 반올림으로 같아지면 1 단위 (0.05V) 차이를 둬
	 *        차압 방향 유지
	 */
	void pressureVsp(uint8_t kind, uint16_t supply, uint16_t exhaust, uint8_t& supplyVsp, uint8_t& exhaustVsp);
}

#endif
