#include "RcDto.h"

#include "PlcService.h"
#include "RcConfig.h"

namespace {

// E5 예약시간 값 -> 분
uint16_t reserveRxToMin(uint8_t v) {
	const uint32_t m = (uint32_t)v * RC_RESERVE_TX_UNIT_MIN;
	return (uint16_t)(m > RC_RESERVE_MAX_MIN ? RC_RESERVE_MAX_MIN : m);
}

}

RcDto::RcDto() {
	setDefaults();
	errCode = 0;
	outdoorTemp = 0;
	mainCo2 = 0;
	heaterRunning = false;
	filterRemain = 0;
	filterUsage = 0;
	filterUsedMin = 0;
	fireAlarm = false;
	heat = false;
	bathLink = false;
	hoodLink = false;
	autoVolume = 1;
	autoClean = false;
	extTemp = 0;
	extHumidity = 0;
	extPm25 = 0;
	extPm10 = 0;
	extTvoc = 0;
	extTemp2 = 0;
	extHumidity2 = 0;
	rxMask = 0;
	linkLost = false;
}

RcDto::RcDto(const RcDto& other) {
	*this = other;
}

RcDto& RcDto::operator=(const RcDto& other) {
	if( this == &other ) {
		return *this;
	}
	mode = other.mode;
	volume = other.volume;
	bypass = other.bypass;
	heater = other.heater;
	clean = other.clean;
	autoDrive = other.autoDrive;
	ahuLock = other.ahuLock;
	exhaust = other.exhaust;
	supply = other.supply;
	night = other.night;
	dust = other.dust;
	aiMode = other.aiMode;
	deviceId = other.deviceId;
	startMin = other.startMin;
	stopMin = other.stopMin;
	filterInit = other.filterInit;
	vspExhaustL = other.vspExhaustL;
	vspExhaustM = other.vspExhaustM;
	vspExhaustH = other.vspExhaustH;
	vspSupplyL = other.vspSupplyL;
	vspSupplyM = other.vspSupplyM;
	vspSupplyH = other.vspSupplyH;
	vspBpExhaustL = other.vspBpExhaustL;
	vspBpExhaustM = other.vspBpExhaustM;
	vspBpExhaustH = other.vspBpExhaustH;
	vspBpSupplyL = other.vspBpSupplyL;
	vspBpSupplyM = other.vspBpSupplyM;
	vspBpSupplyH = other.vspBpSupplyH;
	autoVolume = other.autoVolume;
	autoClean = other.autoClean;

	errCode = other.errCode;
	outdoorTemp = other.outdoorTemp;
	mainCo2 = other.mainCo2;
	heaterRunning = other.heaterRunning;
	filterRemain = other.filterRemain;
	filterUsage = other.filterUsage;
	filterUsedMin = other.filterUsedMin;
	fireAlarm = other.fireAlarm;
	heat = other.heat;
	bathLink = other.bathLink;
	hoodLink = other.hoodLink;

	extTemp = other.extTemp;
	extHumidity = other.extHumidity;
	extPm25 = other.extPm25;
	extPm10 = other.extPm10;
	extTvoc = other.extTvoc;
	extTemp2 = other.extTemp2;
	extHumidity2 = other.extHumidity2;

	rxMask = other.rxMask;
	linkLost = other.linkLost;
	return *this;
}

void RcDto::setDefaults() {
	mode = RC::RUN;
	volume = 1;
	bypass = false;
	heater = false;
	clean = false;
	autoDrive = false;
	ahuLock = false;
	exhaust = false;
	supply = false;
	night = false;
	dust = false;
	aiMode = false;
	deviceId = DEFAULT_DEVICE_ID;
	startMin = 60;
	stopMin = 60;
	filterInit = RC_FILTER_INIT_DEFAULT_X100;
	// 기존 VSP 화면 표시값 (0.05V 단위, MAIN A6 수신 시 갱신)
	vspExhaustL = 32;	// 1.60V
	vspExhaustM = 60;	// 3.00V
	vspExhaustH = 92;	// 4.60V
	vspSupplyL = 36;	// 1.80V
	vspSupplyM = 64;	// 3.20V
	vspSupplyH = 96;	// 4.80V
	vspBpExhaustL = vspExhaustL;
	vspBpExhaustM = vspExhaustM;
	vspBpExhaustH = vspExhaustH;
	vspBpSupplyL = vspSupplyL;
	vspBpSupplyM = vspSupplyM;
	vspBpSupplyH = vspSupplyH;
}

bool RcDto::sameSettings(const RcDto& o) const {
	return mode == o.mode && volume == o.volume && bypass == o.bypass && heater == o.heater &&
		clean == o.clean && autoDrive == o.autoDrive && ahuLock == o.ahuLock &&
		exhaust == o.exhaust && supply == o.supply && night == o.night && dust == o.dust &&
		aiMode == o.aiMode && deviceId == o.deviceId &&
		startMin == o.startMin && stopMin == o.stopMin && filterInit == o.filterInit &&
		vspExhaustL == o.vspExhaustL && vspExhaustM == o.vspExhaustM && vspExhaustH == o.vspExhaustH &&
		vspSupplyL == o.vspSupplyL && vspSupplyM == o.vspSupplyM && vspSupplyH == o.vspSupplyH &&
		vspBpExhaustL == o.vspBpExhaustL && vspBpExhaustM == o.vspBpExhaustM && vspBpExhaustH == o.vspBpExhaustH &&
		vspBpSupplyL == o.vspBpSupplyL && vspBpSupplyM == o.vspBpSupplyM && vspBpSupplyH == o.vspBpSupplyH;
}

bool RcDto::applyUiMode(RC::UiMode m) {
	bypass = false;
	clean = false;
	autoDrive = false;
	aiMode = false;
	switch( m ) {
		case RC::UI_VENTILATION: break;
		case RC::UI_BYPASS: bypass = true; break;
		case RC::UI_CLEAN: clean = true; break;
		case RC::UI_AUTO: autoDrive = true; break;
		case RC::UI_AI: aiMode = true; autoDrive = AI_SETS_AUTO_BIT; break;
		default: return false;
	}
	return true;
}

RC::UiMode RcDto::uiMode() const {
	if( aiMode ) return RC::UI_AI;
	if( bypass ) return RC::UI_BYPASS;
	if( clean ) return RC::UI_CLEAN;
	if( autoDrive ) return RC::UI_AUTO;
	return RC::UI_VENTILATION;
}

void RcDto::setVolume(uint8_t v) {
	volume = v & PLC::FAN_MASK_B5;
}

void RcDto::setDeviceId(uint8_t id) {
	deviceId = id;
}

bool RcDto::isRunning() const {
	return mode == RC::RUN || mode == RC::STOP_RESERVE;
}

bool RcDto::autoActive() const {
	return autoDrive && !aiMode;
}

bool RcDto::hasFilterInfo() const {
	return (rxMask & RC::RX_D6) || RC_FILTER_LOCAL_COUNT;
}

int RcDto::filterRemainHours() const {
	if( rxMask & RC::RX_D6 ) return filterRemain * 100;
	const int remain = filterInit * 100 - (int)(filterUsedMin / 60);
	return remain > 0 ? remain : 0;
}

int RcDto::filterUsedHours() const {
	if( rxMask & RC::RX_D6 ) {
		const int used = filterInit * 100 - filterRemain * 100;
		return used > 0 ? used : 0;
	}
	return (int)(filterUsedMin / 60);
}

int RcDto::filterRemainPct() const {
	if( filterInit == 0 ) return 0;
	const uint32_t totalMin = (uint32_t)filterInit * 100 * 60;
	if( rxMask & RC::RX_D6 ) {
		const int pct = filterRemain * 100 / filterInit;
		return pct > 100 ? 100 : pct;
	}
	if( filterUsedMin >= totalMin ) return 0;
	// 남은 분 기준, 올림 (사용 직후 100% 가 바로 99% 로 내려가지 않게)
	return (int)(((totalMin - filterUsedMin) * 100 + totalMin - 1) / totalMin);
}

bool RcDto::filterReplaceDue() const {
	if( errCode & PLC::FILTER_REPLACE_ALARM ) return true;
	if( (rxMask & RC::RX_D6) || !RC_FILTER_LOCAL_COUNT ) return false;
	return filterUsedMin >= (uint32_t)filterInit * 100 * 60;
}

uint8_t RcDto::effectiveVolume() const {
	if( night ) return RC_SLEEP_FAN;
	if( autoActive() ) return autoVolume;
	return volume;
}

bool RcDto::effectiveClean() const {
	if( autoActive() ) return RC_AUTO_CLEAN_SETS_CLEAN_BIT && autoClean;
	return clean;
}

void RcDto::dropAiIfOverridden() {
	if( aiMode && (bypass || clean || autoDrive != (bool)AI_SETS_AUTO_BIT) ) {
		aiMode = false;
	}
}

bool RcDto::structConverter(const PLC::d5e5b5& packet) {
	if( packet.header == PLC::D5 ) {
		outdoorTemp = (int16_t)packet.data1 - 20;
		mainCo2 = (uint16_t)(packet.data2 * 100 + packet.data3);
		errCode = packet.data4;
		rxMask |= RC::RX_D5;
		return false;
	}
	if( packet.header != PLC::E5 ) {
		return false;
	}

	const RcDto before(*this);
	rxMask |= RC::RX_E5;

	if( packet.data1 != PLC::NO_CHANGE ) {
		const uint8_t m = (packet.data1 & PLC::MODE_MASK_E5) >> 4;
		if( m <= RC::STOP_RESERVE ) {
			mode = m;
		}	// 5: Filter Time reset 은 MAIN 동작, 상태 변경 없음
		bypass = (packet.data1 & PLC::BYPASS_E5) != 0;
		autoDrive = (packet.data1 & PLC::AUTO_E5) != 0;
		ahuLock = (packet.data1 & PLC::LOCK_E5) != 0;
		const uint8_t newVolume = packet.data1 & PLC::FAN_MASK_E5;
		if( newVolume != 0 || !E5_FAN0_KEEPS_FAN ) {
			volume = newVolume;	// 0: 기존 풍량 유지
		}
	}

	if( packet.data2 != PLC::NO_CHANGE && packet.data3 != PLC::NO_CHANGE ) {
		const uint8_t v = packet.data3;
		switch( packet.data2 ) {
			case PLC::TM_FILTER_SET:
				if( !RC_FILTER_INIT_FIXED ) filterInit = v;	// x100h (고정이면 MAIN 값 무시)
				break;
			case PLC::TM_RUN_RSV_SET:
				startMin = reserveRxToMin(v);
				break;
			case PLC::TM_STOP_RSV_SET:
				stopMin = reserveRxToMin(v);
				break;
			case PLC::TM_FUNCTION:
				fireAlarm = (v & PLC::FIRE_E5) != 0;
				heat = (v & PLC::HEAT_E5) != 0;
				dust = (v & PLC::DUST_REMOVE_E5) != 0;
				clean = (v & PLC::CLEAN_E5) != 0;
				heaterRunning = (v & PLC::HEATER_RUN_E5) != 0;
				night = (v & PLC::SLEEP_E5) != 0;
				bathLink = (v & PLC::BATH_E5) != 0;
				hoodLink = (v & PLC::HOOD_E5) != 0;
				break;
			default:
				break;
		}
	}
	dropAiIfOverridden();
	return !sameSettings(before);
}

bool RcDto::structConverter(const PLC::d6b6& packet) {
	heaterRunning = (packet.data1 & PLC::HEATER_RUN_D6) != 0;
	filterRemain = packet.data2;
	filterUsage = RC_FILTER_USAGE(packet.data3, packet.data4);
	rxMask |= RC::RX_D6;
	return false;
}

bool RcDto::structConverter(const PLC::d7e7b7& packet) {
	if( packet.header == PLC::D7 ) {
		rxMask |= RC::RX_D7;
		return false;
	}
	if( packet.header != PLC::E7 ) {
		return false;
	}

	const RcDto before(*this);
	rxMask |= RC::RX_E7;
	const uint8_t v = packet.data3;
	heater = (v & PLC::HEATER_CTRL_D7) != 0;
	clean = (v & PLC::CLEAN_D7) != 0;
	exhaust = (v & PLC::EXHAUST_D7) != 0;
	supply = (v & PLC::SUPPLY_D7) != 0;
	night = (v & PLC::SLEEP_D7) != 0;
	autoDrive = (v & PLC::AUTO_D7) != 0;
	dropAiIfOverridden();
	return !sameSettings(before);
}

bool RcDto::structConverter(const PLC::d8& packet) {
	const int mag = packet.data1 & 0x7F;
	extTemp = (int16_t)((packet.data1 & 0x80) ? -mag : mag);	// b7: 1 음수
	extHumidity = packet.data2;
	extPm25 = packet.data3;
	extPm10 = packet.data4;
	rxMask |= RC::RX_D8;
	return false;
}

bool RcDto::structConverter(const PLC::d9& packet) {
	extTvoc = (uint16_t)((packet.data1 << 8) | packet.data2);
	extTemp2 = (int16_t)packet.data3 - 20;
	extHumidity2 = packet.data4;
	rxMask |= RC::RX_D9;
	return false;
}

bool RcDto::structConverter(const PLC::a6& packet) {
	const RcDto before(*this);
	rxMask |= RC::RX_A6;
	// PLC 값 = V x 20 = 0.05V 단위, 그대로 저장
	const uint8_t l = packet.data2;
	const uint8_t m = packet.data3;
	const uint8_t h = packet.data4;
	switch( packet.data1 ) {
		case PLC::VSP_EXHAUST_HEAT_EXCH:
			vspExhaustL = l; vspExhaustM = m; vspExhaustH = h;
			break;
		case PLC::VSP_SUPPLY_HEAT_EXCH:
			vspSupplyL = l; vspSupplyM = m; vspSupplyH = h;
			break;
		case PLC::VSP_EXHAUST_BYPASS:
			vspBpExhaustL = l; vspBpExhaustM = m; vspBpExhaustH = h;
			break;
		case PLC::VSP_SUPPLY_BYPASS:
			vspBpSupplyL = l; vspBpSupplyM = m; vspBpSupplyH = h;
			break;
		default:
			break;
	}
	return !sameSettings(before);
}

uint8_t RcDto::makeB5Func(bool ctrlFlag) const {
	return static_cast<uint8_t>(
		(bypass    ? PLC::BYPASS_B5    : 0) |
		(heater    ? PLC::HEATER_B5    : 0) |
		(ctrlFlag  ? PLC::CTRL_FLAG_B5 : 0) |
		(effectiveClean() ? PLC::CLEAN_B5 : 0) |
		(autoDrive ? PLC::AUTO_B5      : 0) |
		(ahuLock   ? PLC::LOCK_B5      : 0) |
		(effectiveVolume() & PLC::FAN_MASK_B5));
}

uint8_t RcDto::makeB5Func8() const {
	return static_cast<uint8_t>(
		(exhaust ? PLC::EXHAUST_B5      : 0) |
		(supply  ? PLC::SUPPLY_B5       : 0) |
		(dust    ? PLC::DUST_REMOVE_B5  : 0) |
		(night   ? PLC::SLEEP_B5        : 0));
}

PLC::d5e5b5 RcDto::makeB5(bool ctrlFlag) const {
	PLC::d5e5b5 packet;
	packet.header = PLC::B5;
	packet.data1 = deviceId;
	packet.data2 = mode;
	packet.data3 = makeB5Func(ctrlFlag);
	packet.data4 = 0x00;
	packet.checksum = PlcService::makeChecksum((const uint8_t*)(&packet));
	return packet;
}
