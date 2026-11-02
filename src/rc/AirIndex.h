#ifndef __AIRINDEX_H__
#define __AIRINDEX_H__

#include <cstdint>

#include "RcDto.h"

/**
 * @brief 실내 종합 공기질 지수 (AQI, 0~500)
 *
 * 숫자(AQI)와 색은 따로 계산한다.
 * 숫자: 각 센서 0~500 부속 지수 중 최대. 게이지 270° 채움만 이 값.
 * 색/등급: 각 센서 농도 → 0~100 점수, 점수 > 0 인 항목의 평균.
 *   0~25 좋음, 25~50 보통, 50~75 나쁨, 75~100 매우 나쁨. AQI 와 무관.
 * 센서가 하나도 없으면 NONE ("-").
 */
namespace AQI {
	enum Pollutant : uint8_t {
		PM10 = 0, PM25, TVOC, CO2, CO, POLLUTANT_COUNT
	};

	enum Level : uint8_t {
		NONE = 0,		// 센서값 없음 ("-", 회색)
		GOOD,			// 색 평균 0 ~ 25
		NORMAL,			// 색 평균 25 ~ 50
		BAD,			// 색 평균 50 ~ 75
		VERY_BAD		// 색 평균 75 ~ 100
	};

	struct Result {
		Level level = NONE;
		float aqi = 0;		// 0 ~ 500, 부속 지수 중 최대 (숫자 / 게이지)
		uint8_t count = 0;	// 색 점수 > 0 인 항목 수
	};

	/**
	 * @brief 오염물질 1개 부속 지수 (0 ~ 500, 소수 2자리). 값 <= 0 이면 0
	 */
	float score(Pollutant p, float value);

	/**
	 * @brief AQI -> 등급 (50 좋음, 51 보통, 100 보통, 101 나쁨)
	 */
	Level levelOf(float aqi);

	/**
	 * @brief 룸콘 센서값으로 지수 계산 (TVOC / CO 는 RcConfig 설정에 따라 제외)
	 */
	Result evaluate(const RC::SensorCache& s);
}

#endif
