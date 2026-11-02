#include "Rs485Controller.h"

#include <cstdio>
#include <string>

#include "esp_log.h"
#include "rc/RcConfig.h"
#include "rc/RcController.h"
#include "Rs485Config.h"
#include "Rs485Service.h"

namespace {

const char* TAG = "RS485";

// 전압 x10 (23) -> "2.3"
std::string voltsX10(int v) {
	char b[24];
	snprintf(b, sizeof(b), "%d.%d", v / 10, v % 10);
	return b;
}

} // namespace

Rs485Controller& Rs485Controller::instance() {
	static Rs485Controller s_instance;
	return s_instance;
}

uint8_t Rs485Controller::myId() {
	if( RS485_USE_RC_DEVICE_ID ) {
		const uint8_t id = RcController::instance().getState().deviceId;
		if( id != 0 ) return id;
	}
	return RS485_DEFAULT_ID;
}

bool Rs485Controller::handleRequest(const RS485::Frame& req, RS485::Frame& out) {
	const uint8_t id = myId();
	if( req.group != RS485_GROUP || req.id != id ) return false;	// 다른 장치 주소

	if( RS485_ALLOW_CONTROL ) {
		switch( req.header ) {
			case RS485::E5: applyE5(req); break;
			case RS485::E7: applyE7(req); break;
			case RS485::C5: applyC5(req); break;
			default: break;
		}
	}
	RcController& rc = RcController::instance();
	return Rs485Service::buildReply(req, RS485_GROUP, id, rc.getState(), rc.getSensors(), out);
}

// E5: D0 모드/팬 (0xFE 변화 없음), D2 필터시간, D3/D4 예약시간
void Rs485Controller::applyE5(const RS485::Frame& req) {
	RcController& rc = RcController::instance();
	const RcDto st = rc.getState();
	const uint8_t d0 = req.d[0];

	if( d0 != RS485::NO_CHANGE ) {
		const bool lock = d0 & RS485::LOCK;
		if( !lock && st.ahuLock ) rc.setFunctionState("lock", false);	// 해제를 먼저 (잠금 중엔 다른 명령 거부)

		const int mode = (d0 & RS485::MODE_MASK) >> RS485::MODE_SHIFT;
		const int fan = d0 & RS485::FAN_MASK;
		switch( mode ) {
			case RS485::MODE_OFF:
				if( st.mode != RC::STOP ) rc.setPowerState(false);
				break;
			case RS485::MODE_RUN: {
				if( st.mode != RC::RUN ) rc.setPowerState(true);
				// 모드 열: 자동 > 바이패스 > 전열 (청정 / AI 는 D0 에 없으므로 유지)
				if( d0 & RS485::AUTO ) {
					if( !st.autoActive() ) rc.setModeState("auto");
				} else if( d0 & RS485::BYPASS ) {
					if( !st.bypass ) rc.setModeState("bypass");
				} else if( st.bypass || st.autoActive() ) {
					rc.setModeState("ventilation");
				}
				if( fan >= 1 && fan != st.volume ) rc.setVolumeState(std::to_string(fan));
				break;
			}
			case RS485::MODE_RUN_RSV:
			case RS485::MODE_STOP_RSV: {
				const bool start = (mode == RS485::MODE_RUN_RSV);
				const uint8_t h = req.d[start ? 3 : 4];
				if( h >= 1 && h <= 24 ) {
					rc.setReservationState(std::string(start ? "start_reserve_" : "stop_reserve_") +
										   std::to_string(h * RS485_RESERVE_UNIT_MIN));
				} else {
					ESP_LOGW(TAG, "E5 reservation without valid time (%u)", h);
				}
				break;
			}
			case RS485::MODE_FILTER_RST:
				rc.setFilterReset();
				break;
			case RS485::MODE_ELEM_RST:
				ESP_LOGW(TAG, "E5 element time reset ignored (no element time)");
				break;
			default:
				ESP_LOGW(TAG, "E5 unknown mode %d", mode);
				break;
		}

		if( lock && !st.ahuLock ) rc.setFunctionState("lock", true);	// 잠금은 마지막에
	}

	// D2 필터시간 설정: 초기값 고정 중이면 무시 (초기화는 mode 5), 값이 바뀔 때만 적용
	const uint8_t filter = req.d[2];
	if( !RC_FILTER_INIT_FIXED && filter != RS485::NO_CHANGE && filter != RS485::TIMER_STOP &&
		filter >= RC_FILTER_INIT_MIN && filter <= RC_FILTER_INIT_MAX && filter != st.filterInit ) {
		rc.setFilterInitial(filter);
	}
}

// E7: D3 설정기능 - 바뀐 비트만 적용 (D4 연동기능은 제어 불가)
void Rs485Controller::applyE7(const RS485::Frame& req) {
	RcController& rc = RcController::instance();
	const RcDto st = rc.getState();
	const uint8_t f = req.d[3];
	struct { uint8_t bit; bool now; const char* name; } items[] = {
		{ RS485::HEATER_CTRL, st.heater,	"heater" },
		{ RS485::CLEAN,		st.clean,	"clean" },
		{ RS485::EXHAUST,	st.exhaust,	"exhaust" },
		{ RS485::SUPPLY,	st.supply,	"supply" },
		{ RS485::SLEEP,		st.night,	"sleep" },
	};
	for( const auto& it : items ) {
		const bool want = f & it.bit;
		if( want != it.now ) rc.setFunctionState(it.name, want);
	}
}

// C5: Fan1 (급기) L/M/H, Fan2 (배기) L/M/H - 하나라도 범위 밖이면 전체 무시
void Rs485Controller::applyC5(const RS485::Frame& req) {
	int v[6];
	for( int i = 0 ; i < 6 ; i++ ) {
		v[i] = Rs485Service::decodeVspX10(req.d[i]);
		if( v[i] <= 5 || v[i] >= 55 ) {	// RC 와 같은 범위: 0.5V < V < 5.5V
			ESP_LOGW(TAG, "C5 VSP out of range (D%d = %d)", i, v[i]);
			return;
		}
	}
	RcController::instance().setVspSetting(voltsX10(v[5]), voltsX10(v[4]), voltsX10(v[3]),	// 배기 H/M/L
										   voltsX10(v[2]), voltsX10(v[1]), voltsX10(v[0]));	// 급기 H/M/L
}
