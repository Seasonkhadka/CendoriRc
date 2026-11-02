#include "AirIndex.h"

#include <cmath>

#include "RcConfig.h"

namespace {

// 농도 구간 하한/상한 (표). 마지막 상한은 "이상" 구간을 500점에 맞추는 캡.
struct Breakpoints {
	float lo[4];
	float hi[4];
};

const Breakpoints kTable[AQI::POLLUTANT_COUNT] = {
	{ { 0, 31, 81, 151 }, { 30, 80, 150, 600 } },			// PM10 ug/m3
	{ { 0, 16, 36, 76 }, { 15, 35, 75, 500 } },				// PM2.5 ug/m3
	{ { 0, 0.21f, 0.41f, 1.01f }, { 0.2f, 0.4f, 1.0f, 10.0f } },	// TVOC mg/m3
	{ { 0, 701, 1001, 2001 }, { 700, 1000, 2000, 5000 } },	// CO2 ppm
	{ { 0, 2.1f, 9.1f, 25.1f }, { 2.0f, 9.0f, 25.0f, 50.0f } },	// CO ppm
};

const float kILo[4] = { 0, 51, 101, 201 };
const float kIHi[4] = { 50, 100, 200, 500 };

// 색만: 농도 → 0~100 (앱 표). AQI 0~500 표와 별개.
const Breakpoints kColor[AQI::POLLUTANT_COUNT] = {
	{ { 0, 31, 81, 151 }, { 31, 81, 151, 5000 } },			// PM10 ug/m3
	{ { 0, 16, 36, 76 }, { 16, 36, 76, 5000 } },				// PM2.5 ug/m3
	{ { 0, 201, 401, 1001 }, { 201, 401, 1001, 10000 } },	// TVOC ug/m3
	{ { 0, 700, 1001, 2001 }, { 700, 1001, 2001, 10000 } },	// CO2 ppm
	{ { 0, 2, 10, 30 }, { 2, 10, 30, 200 } },				// CO ppm
};
const float kCLo[4] = { 0, 25, 50, 75 };
const float kCHi[4] = { 25, 50, 75, 100 };

float round2(double v) {
	return (float)(std::floor(v * 100.0 + 0.5 + 1e-7) / 100.0);
}

float mapBands(float value, const Breakpoints& b, const float* iLo, const float* iHi, float cap) {
	if( value <= 0 ) return 0;
	for( int i = 0 ; i < 4 ; i++ ) {
		if( value <= b.hi[i] || i == 3 ) {
			if( i == 3 && value >= b.hi[3] ) return cap;
			const double clo = b.lo[i];
			const double chi = b.hi[i];
			if( chi <= clo ) return iHi[i];
			double v = value < clo ? clo : value;
			return round2((iHi[i] - iLo[i]) / (chi - clo) * (v - clo) + iLo[i]);
		}
	}
	return cap;
}

float colorScore(AQI::Pollutant p, float value) {
	return mapBands(value, kColor[p], kCLo, kCHi, 100);
}

AQI::Level colorLevelOf(float avg) {
	if( avg <= 25.0f ) return AQI::GOOD;
	if( avg <= 50.0f ) return AQI::NORMAL;
	if( avg <= 75.0f ) return AQI::BAD;
	return AQI::VERY_BAD;
}

} // namespace

namespace AQI {

float score(Pollutant p, float value) {
	return mapBands(value, kTable[p], kILo, kIHi, 500);
}

Level levelOf(float aqi) {
	if( aqi <= 50.0f ) return GOOD;
	if( aqi <= 100.0f ) return NORMAL;
	if( aqi <= 200.0f ) return BAD;
	return VERY_BAD;
}

Result evaluate(const RC::SensorCache& s) {
	Result r;
	float best = 0;
	float colorSum = 0;
	bool any = false;

	auto takeAqi = [&](Pollutant p, float value) {
		const float sc = score(p, value);
		if( sc > best ) best = sc;
	};
	auto takeColor = [&](Pollutant p, float value) {
		const float cs = colorScore(p, value);
		if( cs > 0 ) {
			r.count++;
			colorSum += cs;
		}
	};

	if( s.hasPm ) {
		takeAqi(PM10, (float)s.pm10);
		takeAqi(PM25, (float)s.pm25);
		takeColor(PM10, (float)s.pm10);
		takeColor(PM25, (float)s.pm25);
		any = true;
	}
	if( s.hasCo2 ) {
		takeAqi(CO2, (float)s.co2);
		takeColor(CO2, (float)s.co2);
		any = true;
	}
	if( RC_AQI_USE_TVOC && s.hasVoc ) {
		takeAqi(TVOC, (float)s.tvoc / 1000.0f);	// ug/m3 -> mg/m3 (AQI 표)
		takeColor(TVOC, (float)s.tvoc);			// ug/m3 (색 표)
		any = true;
	}
	if( RC_AQI_USE_CO && s.hasCo ) {
		takeAqi(CO, (float)s.co);
		takeColor(CO, (float)s.co);
		any = true;
	}
	if( !any ) return r;

	r.aqi = best;
	r.level = r.count ? colorLevelOf(colorSum / (float)r.count) : GOOD;
	return r;
}

} // namespace AQI
