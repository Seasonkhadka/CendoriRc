#include "PlcService.h"

#include <cmath>
#include <cstdio>

#include "RcConfig.h"

namespace {

uint8_t clampByte(int v) {
	if( v < 0 ) return 0;
	if( v > 255 ) return 255;
	return (uint8_t)v;
}

}

uint8_t PlcService::makeChecksum(const uint8_t* buf) {
	uint8_t sum = 0;
	for( int i = 0 ; i < PLC::FRAME_LEN - 1 ; i++ ) {
		sum += buf[i];
	}
	return (uint8_t)(0x100 - sum);	// 2의 보수
}

bool PlcService::checkChecksum(const uint8_t* buf) {
	uint8_t sum = 0;
	for( int i = 0 ; i < PLC::FRAME_LEN ; i++ ) {
		sum += buf[i];
	}
	return sum == 0;
}

uint8_t PlcService::encodeReserve(uint16_t minutes) {
	uint32_t v = ((uint32_t)minutes + RC_RESERVE_TX_UNIT_MIN - 1) / RC_RESERVE_TX_UNIT_MIN;
	if( v > RC_RESERVE_TX_MAX ) v = RC_RESERVE_TX_MAX;
	return (uint8_t)v;
}

PLC::d5e5b5 PlcService::makeB5Time(const RcDto& dto, uint8_t timeSel, uint16_t minutes, bool ctrlFlag) {
	PLC::d5e5b5 packet;
	packet.header = PLC::B5;
	packet.data1 = dto.deviceId;
	packet.data2 = timeSel;
	packet.data3 = dto.makeB5Func(ctrlFlag);
	packet.data4 = encodeReserve(minutes);
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::d5e5b5 PlcService::makeB5Filter(const RcDto& dto, bool ctrlFlag) {
	PLC::d5e5b5 packet;
	packet.header = PLC::B5;
	packet.data1 = dto.deviceId;
	packet.data2 = PLC::B5_FILTER_TIME;
	packet.data3 = dto.makeB5Func(ctrlFlag);
	packet.data4 = dto.filterInit;
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::d5e5b5 PlcService::makeB5Function(const RcDto& dto, bool ctrlFlag) {
	PLC::d5e5b5 packet;
	packet.header = PLC::B5;
	packet.data1 = dto.deviceId;
	packet.data2 = PLC::B5_FUNCTION;
	packet.data3 = dto.makeB5Func(ctrlFlag);
	packet.data4 = dto.makeB5Func8();
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::c5 PlcService::makeC5(uint8_t vspMode, uint8_t low, uint8_t mid, uint8_t high) {
	PLC::c5 packet;
	packet.header = PLC::C5;
	packet.data1 = vspMode;
	packet.data2 = low;	// V x 20
	packet.data3 = mid;
	packet.data4 = high;
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::d6b6 PlcService::makeB6(const RC::SensorCache& sensor) {
	PLC::d6b6 packet;
	packet.header = PLC::B6;
	const int co2 = sensor.hasCo2 ? sensor.co2 / RC_B6_CO2_DIV : 0;
	packet.data1 = (uint8_t)(co2 < 0 ? 0 : (co2 > 250 ? 250 : co2));	// 0~5000ppm
	packet.data2 = sensor.hasPm ? clampByte(sensor.pm25) : 0;
	const uint16_t tvoc = sensor.hasVoc ? (uint16_t)(sensor.tvoc < 0 ? 0 : sensor.tvoc) : 0;
	packet.data3 = (uint8_t)(tvoc >> 8);
	packet.data4 = (uint8_t)(tvoc & 0xFF);
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::d7e7b7 PlcService::makeB7(const RcDto& dto, const RC::SensorCache& sensor) {
	PLC::d7e7b7 packet;
	packet.header = PLC::B7;
	packet.data1 = sensor.hasTemp ? clampByte((int)std::lround(sensor.tempC) + 20) : 0;
	packet.data2 = sensor.hasPm ? clampByte(sensor.pm10) : 0;
	packet.data3 = static_cast<uint8_t>(
		((dto.errCode & RC_B7_FILTER_ALARM_MASK) ? PLC::FILTER_ALARM_B7 : 0) |
		(dto.exhaust ? PLC::EXHAUST_B7 : 0) |
		(dto.supply  ? PLC::SUPPLY_B7  : 0) |
		(dto.night   ? PLC::SLEEP_B7   : 0));
	int rh = sensor.hasTemp ? (int)std::lround(sensor.humidity) : 0;
	packet.data4 = (uint8_t)(rh < 0 ? 0 : (rh > 99 ? 99 : rh));
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

PLC::b8 PlcService::makeB8(const RC::SensorCache& sensor) {
	PLC::b8 packet;
	const uint16_t co = sensor.hasCo ? (uint16_t)(sensor.co < 0 ? 0 : sensor.co) : 0;
	packet.data1 = (uint8_t)(co >> 8);
	packet.data2 = (uint8_t)(co & 0xFF);
	packet.data3 = 0x00;
	packet.data4 = 0x00;
	packet.checksum = makeChecksum((const uint8_t*)(&packet));
	return packet;
}

std::string PlcService::toHex(const uint8_t* buf, size_t len) {
	std::string out;
	out.reserve(len * 3);
	char tmp[4];
	for( size_t i = 0 ; i < len ; i++ ) {
		snprintf(tmp, sizeof(tmp), i == 0 ? "%02X" : " %02X", buf[i]);
		out += tmp;
	}
	return out;
}
