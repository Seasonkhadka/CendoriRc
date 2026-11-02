#include "RcAuto.h"

#include "RcConfig.h"

namespace RC {

namespace {

AirLevel level4(int v, int good, int normal, int bad) {
	if( v <= good ) return AIR_GOOD;
	if( v <= normal ) return AIR_NORMAL;
	if( v <= bad ) return AIR_BAD;
	return AIR_VERY_BAD;
}

}

AirLevel levelVocPpb(int ppb) { return level4(ppb, 220, 660, 2200); }
AirLevel levelCo2(int ppm) { return level4(ppm, 700, 1000, 1500); }
AirLevel levelPm25(int ug) { return level4(ug, 15, 35, 75); }
AirLevel levelPm10(int ug) { return level4(ug, 30, 80, 120); }

AutoResult autoDecide(bool hasCo2, int co2, bool hasPm, int pm25, int pm10) {
	if( !hasCo2 && !hasPm ) {
		return { (uint8_t)RC_AUTO_NO_SENSOR_FAN, false };
	}
	const AirLevel c = hasCo2 ? levelCo2(co2) : AIR_GOOD;
	AirLevel p = AIR_GOOD;
	if( hasPm ) {
		p = levelPm25(pm25);
		if( RC_AUTO_PM_USES_PM10 ) {
			p = worse(p, levelPm10(pm10));
		}
	}

	// CO2 나쁨 이상: 환기 우선
	if( c == AIR_VERY_BAD ) return { 3, false };
	if( c == AIR_BAD ) return { 2, false };

	// CO2 보통 / 좋음: 미세먼지 나쁘면 청정
	if( p == AIR_VERY_BAD ) return { 3, true };
	if( p == AIR_BAD ) return { 2, true };
	if( p == AIR_NORMAL ) return { 1, true };

	// 둘 다 양호: CO2 보통이면 약풍 환기, 좋음이면 정지
	if( c == AIR_NORMAL ) return { 1, false };
	return { 0, false };
}

}
