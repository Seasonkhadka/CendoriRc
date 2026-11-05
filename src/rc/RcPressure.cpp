#include "RcPressure.h"

#include "RcDto.h"

namespace RC {

namespace {

// 양압: 사무 공간 (1인당 외기량)
const PressureRoom kPositiveRooms[] = {
	{ "개방형 사무실", "Open office",    COUNT_PEOPLE, RC_PRESS_PER_PERSON_M3H, RC_PRESS_OPEN_OFFICE_PCT, 10, 99 },
	{ "회의실",        "Meeting room",   COUNT_PEOPLE, RC_PRESS_PER_PERSON_M3H, RC_PRESS_MEETING_PCT,     12, 99 },
	{ "개인 사무실",   "Private office", COUNT_PEOPLE, RC_PRESS_PER_PERSON_M3H, RC_PRESS_PRIVATE_PCT,      1, 99 },
};

// 음압: 오염원이 있는 공간
const PressureRoom kNegativeRooms[] = {
	{ "화장실",        "Restroom",       COUNT_TOILETS, RC_PRESS_PER_TOILET_M3H, RC_PRESS_RESTROOM_PCT,   2, 20 },
	{ "기타 공간",     "Other room",     COUNT_PEOPLE,  RC_PRESS_PER_PERSON_M3H, RC_PRESS_OTHER_NEG_PCT,  2, 99 },
};

uint16_t roundUp(uint32_t v) {
	const uint32_t s = RC_PRESS_ROUND_M3H;
	return (uint16_t)((v + s - 1) / s * s);
}

uint16_t roundDown(uint32_t v) {
	const uint32_t s = RC_PRESS_ROUND_M3H;
	return (uint16_t)(v / s * s);
}

}

const PressureRoom* pressureRooms(uint8_t kind, int& n) {
	if( kind == PRESS_NEGATIVE ) {
		n = (int)(sizeof(kNegativeRooms) / sizeof(kNegativeRooms[0]));
		return kNegativeRooms;
	}
	n = (int)(sizeof(kPositiveRooms) / sizeof(kPositiveRooms[0]));
	return kPositiveRooms;
}

PressureSetup pressureCompute(uint8_t kind, uint8_t room, uint8_t count, uint16_t ratedM3h) {
	PressureSetup r = {};
	int n = 0;
	const PressureRoom* rooms = pressureRooms(kind, n);
	if( (kind != PRESS_POSITIVE && kind != PRESS_NEGATIVE) || room >= n ) return r;
	const PressureRoom& rt = rooms[room];
	if( count < 1 ) count = 1;
	if( count > rt.maxCount ) count = rt.maxCount;

	uint32_t need = (uint32_t)count * rt.perUnit;
	if( need < RC_PRESS_MIN_M3H ) need = RC_PRESS_MIN_M3H;
	uint16_t high = roundUp(need);
	r.needed = high;
	if( high > ratedM3h ) {
		high = ratedM3h;
		r.capped = true;
	}
	uint16_t low = roundDown((uint32_t)high * 100 / (100 + rt.offsetPct));
	if( low + RC_PRESS_ROUND_M3H > high ) low = high - RC_PRESS_ROUND_M3H;	// 차압 방향 유지

	r.valid = true;
	r.room = room;
	r.count = count;
	r.offsetPct = rt.offsetPct;
	r.supply = (kind == PRESS_POSITIVE) ? high : low;
	r.exhaust = (kind == PRESS_POSITIVE) ? low : high;
	return r;
}

void pressureBoost(uint16_t& supply, uint16_t& exhaust, uint16_t ratedM3h) {
	uint32_t s = (uint32_t)supply * (100 + RC_PRESS_BOOST_PCT) / 100;
	uint32_t e = (uint32_t)exhaust * (100 + RC_PRESS_BOOST_PCT) / 100;
	const uint32_t high = s > e ? s : e;
	if( high > ratedM3h ) {		// 정격 초과 안 함 - 같은 비율로 줄임
		s = s * ratedM3h / high;
		e = e * ratedM3h / high;
	}
	supply = (uint16_t)s;
	exhaust = (uint16_t)e;
}

uint8_t airflowToVsp(uint16_t m3h) {
	const int32_t span = RC_FAN_VSP_AT_RATED - RC_FAN_VSP_AT_ZERO;
	int32_t u = RC_FAN_VSP_AT_ZERO + ((int32_t)m3h * span + RC_FAN_RATED_M3H / 2) / RC_FAN_RATED_M3H;
	if( u <= VSP_MIN ) u = VSP_MIN + 1;
	if( u >= VSP_MAX ) u = VSP_MAX - 1;
	return (uint8_t)u;
}

void pressureVsp(uint8_t kind, uint16_t supply, uint16_t exhaust, uint8_t& supplyVsp, uint8_t& exhaustVsp) {
	supplyVsp = airflowToVsp(supply);
	exhaustVsp = airflowToVsp(exhaust);
	// 큰 쪽 팬을 1 단위 올림, 위가 막히면 작은 쪽을 내림
	if( kind == PRESS_POSITIVE && supplyVsp <= exhaustVsp ) {
		if( exhaustVsp + 1 < VSP_MAX ) supplyVsp = exhaustVsp + 1;
		else { supplyVsp = VSP_MAX - 1; exhaustVsp = supplyVsp - 1; }
	} else if( kind == PRESS_NEGATIVE && exhaustVsp <= supplyVsp ) {
		if( supplyVsp + 1 < VSP_MAX ) exhaustVsp = supplyVsp + 1;
		else { exhaustVsp = VSP_MAX - 1; supplyVsp = exhaustVsp - 1; }
	}
}

}
