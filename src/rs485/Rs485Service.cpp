#include "Rs485Service.h"

#include <cstdio>

#include "rc/RcConfig.h"
#include "Rs485Config.h"

namespace {

uint8_t clampByte(int v) {
	if( v < 0 ) return 0;
	if( v > 255 ) return 255;
	return (uint8_t)v;
}

// 온도 + 20 (-20 ~ 60 ℃)
uint8_t encodeTemp(float c) {
	int t = (int)(c >= 0 ? c + 0.5f : c - 0.5f);
	if( t < -20 ) t = -20;
	if( t > 60 ) t = 60;
	return (uint8_t)(t + 20);
}

// CO2: 상위 = ppm / 100, 하위 = ppm % 100
void encodeCo2(int ppm, uint8_t& hi, uint8_t& lo) {
	if( ppm < 0 ) ppm = 0;
	if( ppm > 25599 ) ppm = 25599;
	hi = (uint8_t)(ppm / 100);
	lo = (uint8_t)(ppm % 100);
}

void encodeU16(int v, uint8_t& hi, uint8_t& lo) {
	if( v < 0 ) v = 0;
	if( v > 0xFFFF ) v = 0xFFFF;
	hi = (uint8_t)(v >> 8);
	lo = (uint8_t)(v & 0xFF);
}

// B5 D0: Bypass / Mode / Auto / Lock / 팬속도
uint8_t makeModeByte(const RcDto& dto) {
	uint8_t d0 = 0;
	if( dto.bypass ) d0 |= RS485::BYPASS;
	d0 |= (uint8_t)((dto.mode & 0x07) << RS485::MODE_SHIFT);
	if( dto.autoActive() ) d0 |= RS485::AUTO;
	if( dto.ahuLock ) d0 |= RS485::LOCK;
	if( dto.isRunning() ) d0 |= dto.effectiveVolume() & RS485::FAN_MASK;	// 정지 시 팬 0 (문서 예제)
	return d0;
}

// B5 D5: 메인 에러 비트 -> 외부 프로토콜 에러 비트
uint8_t makeErrorByte(const RcDto& dto) {
	uint8_t e = 0;
	if( dto.errCode & PLC::SUPPLY_FAN_ERR ) e |= RS485::FAN2_SUPPLY_ERR;
	if( dto.errCode & PLC::EXHAUST_FAN_ERR ) e |= RS485::FAN1_EXHAUST_ERR;
	if( RS485_LINK_ERR_FROM_PLC && dto.linkLost ) e |= RS485::COMM_ERR;
	return e;
}

uint8_t makeFuncByte(const RcDto& dto) {
	uint8_t f = 0;
	if( dto.heater ) f |= RS485::HEATER_CTRL;
	if( dto.heaterRunning ) f |= RS485::HEATER_USED;
	if( dto.effectiveClean() ) f |= RS485::CLEAN;
	if( dto.exhaust ) f |= RS485::EXHAUST;
	if( dto.supply ) f |= RS485::SUPPLY;
	if( dto.night ) f |= RS485::SLEEP;
	return f;
}

uint8_t makeAlarmByte(const RcDto& dto) {
	uint8_t a = 0;
	if( dto.filterReplaceDue() ) a |= RS485::FILTER_REPLACE;
	if( dto.errCode & PLC::FILTER_CLEAN_ALARM ) a |= RS485::FILTER_CLEAN;
	if( dto.errCode & PLC::FILTER_ALARM_STOP ) a |= RS485::FILTER_STOP;
	if( dto.fireAlarm ) a |= RS485::FIRE;
	return a;
}

} // namespace

namespace Rs485Service {

uint8_t makeChecksum(const RS485::Frame& f) {
	const uint8_t* p = (const uint8_t*)&f;
	uint8_t x = 0;
	for( int i = 0 ; i < RS485::FRAME_LEN - 1 ; i++ ) x ^= p[i];
	return x;
}

bool checkChecksum(const RS485::Frame& f) {
	return makeChecksum(f) == f.checksum;
}

bool isRequestHeader(uint8_t h) {
	switch( h ) {
		case RS485::D5: case RS485::E5: case RS485::D6: case RS485::D7: case RS485::E7:
		case RS485::D8: case RS485::DB: case RS485::DC: case RS485::D9: case RS485::C5: case RS485::C6:
			return true;
		default:
			return false;
	}
}

uint8_t encodeVsp(uint8_t units005) {
	const int x10 = (units005 + 1) / 2;	// 0.05V -> 0.1V 반올림
	if( RS485_VSP_X10 ) return (uint8_t)x10;
	return (uint8_t)(((x10 / 10) << 4) | (x10 % 10));
}

int decodeVspX10(uint8_t value) {
	if( RS485_VSP_X10 ) return value;
	return (value >> 4) * 10 + (value & 0x0F);
}

uint8_t reserveToWire(uint16_t minutes) {
	const int h = (minutes + RS485_RESERVE_UNIT_MIN - 1) / RS485_RESERVE_UNIT_MIN;
	return (uint8_t)(h > 24 ? 24 : h);
}

bool buildReply(const RS485::Frame& req, uint8_t group, uint8_t id, const RcDto& dto,
				const RC::SensorCache& sensor, RS485::Frame& out) {
	out = RS485::Frame();
	out.group = group;
	out.id = id;
	uint8_t* d = out.d;

	switch( req.header ) {
		case RS485::D5:
		case RS485::E5: {
			out.header = RS485::B5;
			d[0] = makeModeByte(dto);
			d[1] = RS485_ELEMENT_TIME;
			// 필터 잔여시간 - RC 화면과 같은 값, 100시간 단위 올림 (0 이 되면 교체)
			const int remainH = dto.hasFilterInfo() ? dto.filterRemainHours() : 0;
			d[2] = clampByte((remainH + RS485_FILTER_UNIT_H - 1) / RS485_FILTER_UNIT_H);
			d[3] = reserveToWire(dto.startMin);
			d[4] = reserveToWire(dto.stopMin);
			d[5] = makeErrorByte(dto);
			break;
		}
		case RS485::D6:
			out.header = RS485::B6;
			d[0] = sensor.hasPm ? clampByte(sensor.pm10) : 0;
			if( sensor.hasCo2 ) encodeCo2(sensor.co2, d[1], d[2]);
			d[3] = sensor.hasPm ? clampByte(sensor.pm25) : 0;
			if( sensor.hasVoc ) encodeU16(sensor.tvoc, d[4], d[5]);	// ug/m3
			break;
		case RS485::D7:
		case RS485::E7:
			out.header = RS485::B7;
			d[1] = sensor.hasTemp ? encodeTemp(sensor.tempC) : 0;
			d[2] = sensor.hasTemp ? clampByte((int)(sensor.humidity + 0.5f)) : 0;
			d[3] = makeFuncByte(dto);
			d[4] = (dto.bathLink ? RS485::BATH : 0) | (dto.hoodLink ? RS485::HOOD : 0);
			d[5] = makeAlarmByte(dto);
			break;
		case RS485::D8:
			// 통합센서 (CRD-SEN) 없음: MAIN 이 PLC 로 준 값만, 없으면 0
			out.header = RS485::B8;
			if( dto.rxMask & RC::RX_D8 ) {
				d[0] = dto.extPm25;
				d[1] = dto.extPm10;
			}
			if( dto.rxMask & RC::RX_D5 ) {
				encodeCo2(dto.mainCo2, d[2], d[3]);
				d[4] = encodeTemp(dto.outdoorTemp);
			}
			break;
		case RS485::DB:
			out.header = RS485::BB;
			d[0] = sensor.hasCo ? clampByte(sensor.co) : 0;
			if( sensor.hasVoc ) encodeU16(sensor.tvoc, d[1], d[2]);
			d[3] = sensor.hasPm ? clampByte(sensor.pm1) : 0;
			// D4/D5 차압센서 없음 - 0
			break;
		case RS485::DC:
			out.header = RS485::BC;
			d[1] = sensor.hasCo ? clampByte(sensor.co) : 0;
			break;
		case RS485::D9:
			out.header = RS485::B9;	// 팬 RPM 없음 - 0
			break;
		case RS485::C5:
		case RS485::C6:
			// Fan1 = Motor1 (급기), Fan2 = Motor2 (배기), 전열 VSP
			out.header = RS485::A5;
			d[0] = encodeVsp(dto.vspSupplyL);
			d[1] = encodeVsp(dto.vspSupplyM);
			d[2] = encodeVsp(dto.vspSupplyH);
			d[3] = encodeVsp(dto.vspExhaustL);
			d[4] = encodeVsp(dto.vspExhaustM);
			d[5] = encodeVsp(dto.vspExhaustH);
			break;
		default:
			return false;
	}
	out.checksum = makeChecksum(out);
	return true;
}

void toHex(const RS485::Frame& f, char* buf, int len) {
	const uint8_t* p = (const uint8_t*)&f;
	int n = 0;
	for( int i = 0 ; i < RS485::FRAME_LEN && n + 3 < len ; i++ ) {
		n += snprintf(buf + n, len - n, i ? " %02X" : "%02X", p[i]);
	}
}

} // namespace Rs485Service
