#ifndef __RCAUTO_H__
#define __RCAUTO_H__

#include <cstdint>

/**
 * @brief 공기질 상태 판정 + 자동 모드 결정 (SHR-L2 승인원 2.4.5, Table 1~4)
 */
namespace RC {

	enum AirLevel {
		AIR_GOOD = 0, AIR_NORMAL = 1, AIR_BAD = 2, AIR_VERY_BAD = 3	// 좋음 / 보통 / 나쁨 / 매우나쁨
	};

	AirLevel levelVocPpb(int ppb);	// Table 1: ~220 / ~660 / ~2200 / 2201~
	AirLevel levelCo2(int ppm);		// Table 2: ~700 / ~1000 / ~1500 / 1501~
	AirLevel levelPm25(int ug);		// Table 3: ~15 / ~35 / ~75 / 76~
	AirLevel levelPm10(int ug);		// Table 3: ~30 / ~80 / ~120 / 121~

	inline AirLevel worse(AirLevel a, AirLevel b) { return a > b ? a : b; }

	typedef struct {
		uint8_t volume;	// 0: 정지, 1~3
		bool clean;		// false: 환기, true: 청정
	} AutoResult;

	/**
	 * @brief Table 4: CO2 와 미세먼지 중 나쁜 상태로 풍량 / 환기·청정 결정
	 *
	 * 값이 없는 센서는 좋음으로 본다. 둘 다 없으면 RC_AUTO_NO_SENSOR_FAN 환기.
	 */
	AutoResult autoDecide(bool hasCo2, int co2, bool hasPm, int pm25, int pm10);
}

#endif
