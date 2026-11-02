#include "RcController.h"

#include <cmath>
#include <cstdlib>

#include "io/plc_protocol.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "PlcService.h"
#include "port/esp_time.h"
#include "RcAuto.h"
#include "RcConfig.h"

namespace {

const char* NVS_NAMESPACE = "rc";
const char* NVS_KEY = "settings";
const uint8_t NVS_VERSION = 3;

// NVS 저장 형식 (uint8_t 만 사용, 패딩 없음)
typedef struct {
	uint8_t version;
	uint8_t mode;
	uint8_t volume;
	uint8_t flags;		// FlagBit
	uint8_t flags2;	// FlagBit2
	uint8_t deviceId;
	uint8_t startMinLo;
	uint8_t startMinHi;
	uint8_t stopMinLo;
	uint8_t stopMinHi;
	uint8_t filterInit;
	uint8_t vsp[12];	// 전열 배기 L/M/H, 전열 급기 L/M/H, 바이패스 배기 L/M/H, 바이패스 급기 L/M/H (0.05V)
} SettingsBlob;

enum FlagBit {
	FB_BYPASS = 0x01, FB_HEATER = 0x02, FB_CLEAN = 0x04, FB_AUTO = 0x08,
	FB_LOCK = 0x10, FB_EXHAUST = 0x20, FB_SUPPLY = 0x40, FB_NIGHT = 0x80
};

enum FlagBit2 {
	FB2_DUST = 0x01, FB2_AI = 0x02
};

class Guard {
public:
	explicit Guard(SemaphoreHandle_t m) : m_m(m) { xSemaphoreTake(m_m, portMAX_DELAY); }
	~Guard() { xSemaphoreGive(m_m); }
private:
	SemaphoreHandle_t m_m;
};

bool vspValid(uint8_t units) {
	return units > RC::VSP_MIN && units < RC::VSP_MAX;
}

// "2.45" -> 49 (0.05V 단위 = V x 20). 범위 밖이거나 숫자가 아니면 false
bool parseVsp(const std::string& s, uint8_t& out) {
	if( s.empty() ) return false;
	char* end = nullptr;
	const float v = strtof(s.c_str(), &end);
	if( end == s.c_str() || *end != '\0' ) return false;
	const long u = std::lround(v * 20.0f);
	if( u <= RC::VSP_MIN || u >= RC::VSP_MAX ) return false;
	out = (uint8_t)u;
	return true;
}

bool parseUint(const std::string& s, long minV, long maxV, long& out) {
	if( s.empty() ) return false;
	char* end = nullptr;
	const long v = strtol(s.c_str(), &end, 10);
	if( end == s.c_str() || *end != '\0' || v < minV || v > maxV ) return false;
	out = v;
	return true;
}

}

RcController& RcController::instance() {
	static RcController s_instance;
	return s_instance;
}

RcController::RcController()
	: m_mutex(xSemaphoreCreateMutex()),
	  m_ctrlPending(false),
	  m_funcPending(false),
	  m_filterPending(false),
	  m_autoValid(false),
	  m_autoChangedMs(0),
	  m_resvState(RESV_NONE),
	  m_resvSel(PLC::B5_RUN_RSV_TM),
	  m_vspPending(0),
	  m_nvsDirty(false),
	  m_lastChangeMs(0),
	  m_refreshPending(true),
	  m_linkLost(false),
	  m_filterTickMs(0),
	  m_filterRunMs(0) {
}

void RcController::init() {
	esp_err_t err = nvs_flash_init();
	if( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND ) {
		nvs_flash_erase();
		err = nvs_flash_init();
	}

	RcDto loaded;
	const bool ok = (err == ESP_OK) && loadSettings(loaded);
	if( !ok ) {
		loaded.setDefaults();
	}
	loaded.filterUsedMin = (err == ESP_OK) ? loadFilterMinutes() : 0;
	{
		Guard g(m_mutex);
		m_dto = loaded;
	}
	LOGI("RC settings %s: mode=%u vol=%u id=0x%02X", ok ? "loaded" : "defaults",
		(unsigned)loaded.mode, (unsigned)loaded.volume, (unsigned)loaded.deviceId);
	LOGI("Filter used %u h %u min of %u h", (unsigned)(loaded.filterUsedMin / 60),
		(unsigned)(loaded.filterUsedMin % 60), (unsigned)loaded.filterInit * 100);
	notifyUi();
}

bool RcController::lockedOut(bool unlocking) const {
	return m_dto.ahuLock && !unlocking;
}

void RcController::markLocalChange() {
	m_ctrlPending = RC_CTRL_FLAG_ON_LOCAL_CHANGE;
	m_nvsDirty = true;
	m_lastChangeMs = millis();
}

bool RcController::setPowerState(bool powerOn) {
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		if( powerOn ) {
			m_dto.mode = RC::RUN;
			if( m_dto.volume == 0 ) {
				m_dto.volume = 1;
			}
		} else {
			m_dto.mode = RC::STOP;	// 풍량은 다음 가동을 위해 유지
		}
		m_resvState = RESV_NONE;
		markLocalChange();
	}
	notifyUi();
	return true;
}

bool RcController::setModeState(const std::string& mode) {
	RC::UiMode m;
	if( mode == "ventilation" ) m = RC::UI_VENTILATION;
	else if( mode == "bypass" ) m = RC::UI_BYPASS;
	else if( mode == "clean" ) m = RC::UI_CLEAN;
	else if( mode == "auto" ) m = RC::UI_AUTO;
	else if( mode == "ai" ) m = RC::UI_AI;
	else {
		LOGE("Unknown mode: %s", mode.c_str());
		return false;
	}
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		if( !m_dto.isRunning() ) {
			LOGW("Mode change ignored: power off");
			return false;
		}
		m_dto.applyUiMode(m);	// 모드 비트만 변경, 풍량/전원/잠금/히터 유지
		markLocalChange();
		evaluateAuto();
	}
	notifyUi();
	return true;
}

bool RcController::setVolumeState(const std::string& volume) {
	long v = 0;
	if( !parseUint(volume, 1, RC::VOLUME_MAX, v) ) {	// UI 는 0 을 쓰지 않음 (정지 = 전원 Off)
		LOGE("Volume out of range: %s", volume.c_str());
		return false;
	}
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		if( m_dto.autoActive() || m_dto.night ) {
			LOGW("Fan change ignored: %s", m_dto.night ? "sleep" : "auto");
			return false;
		}
		m_dto.setVolume((uint8_t)v);	// 풍량만 변경, 모드 유지
		markLocalChange();
	}
	notifyUi();
	return true;
}

bool RcController::setDeviceId(int id) {
	if( id < DEVICE_ID_MIN || id > DEVICE_ID_MAX ) {
		LOGE("Device ID out of range: %d", id);
		return false;
	}
	{
		Guard g(m_mutex);
		m_dto.setDeviceId((uint8_t)id);
		m_nvsDirty = true;
		m_lastChangeMs = millis();
	}
	LOGI("Device ID set: %d (0x%02X)", id, id);
	notifyUi();
	return true;
}

bool RcController::setReservationState(const std::string& reservation) {
	static const std::string START = "start_reserve_";
	static const std::string STOP = "stop_reserve_";

	if( reservation == "sleep" || reservation == "cancel" ) {
		const bool sleep = (reservation == "sleep");
		{
			Guard g(m_mutex);
			if( lockedOut(false) ) return false;
			if( sleep ) {
				m_dto.night = true;	// 취침: 항상 약풍 (effectiveVolume)
			} else {
				// 취소: 예약 대기 상태를 되돌리고 취침 해제
				if( m_dto.mode == RC::START_RESERVE ) m_dto.mode = RC::STOP;
				else if( m_dto.mode == RC::STOP_RESERVE ) m_dto.mode = RC::RUN;
				m_dto.night = false;
				m_resvState = RESV_NONE;
			}
			if( RC_SEND_FUNCTION_FRAME ) m_funcPending = true;
			markLocalChange();
		}
		notifyUi();
		return true;
	}

	bool isStart;
	std::string minutes;
	if( reservation.compare(0, START.size(), START) == 0 ) {
		isStart = true;
		minutes = reservation.substr(START.size());
	} else if( reservation.compare(0, STOP.size(), STOP) == 0 ) {
		isStart = false;
		minutes = reservation.substr(STOP.size());
	} else {
		LOGE("Unknown reservation: %s", reservation.c_str());
		return false;
	}
	long m = 0;
	if( !parseUint(minutes, RC_RESERVE_STEP_MIN, RC_RESERVE_MAX_MIN, m) || (m % RC_RESERVE_STEP_MIN) != 0 ) {
		LOGE("Reservation time invalid: %s", reservation.c_str());
		return false;
	}

	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		if( isStart ) {
			m_dto.mode = RC::START_RESERVE;
			m_dto.startMin = (uint16_t)m;
			m_resvSel = PLC::B5_RUN_RSV_TM;
		} else {
			m_dto.mode = RC::STOP_RESERVE;
			m_dto.stopMin = (uint16_t)m;
			m_resvSel = PLC::B5_STOP_RSV_TM;
		}
		if( RC_SEND_RESERVE_TIME_FRAME ) {
			m_resvState = RC_RESERVE_TIME_AFTER_MODE ? RESV_ARMED : RESV_READY;
		}
		markLocalChange();
	}
	notifyUi();
	return true;
}

bool RcController::setVspSetting(const std::string& exhstH, const std::string& exhstM, const std::string& exhstL,
				const std::string& airSplH, const std::string& airSplM, const std::string& airSplL) {
	const std::string v[6] = { exhstH, exhstM, exhstL, airSplH, airSplM, airSplL };
	return setVsp(false, v);
}

bool RcController::setVspBypassSetting(const std::string& exhstH, const std::string& exhstM, const std::string& exhstL,
				const std::string& airSplH, const std::string& airSplM, const std::string& airSplL) {
	const std::string v[6] = { exhstH, exhstM, exhstL, airSplH, airSplM, airSplL };
	return setVsp(true, v);
}

// volts: 배기 H/M/L, 급기 H/M/L
bool RcController::setVsp(bool bypass, const std::string* volts) {
	uint8_t u[6];
	for( int i = 0 ; i < 6 ; i++ ) {
		if( !parseVsp(volts[i], u[i]) ) {
			LOGE("VSP out of range: %s", volts[i].c_str());
			return false;
		}
	}
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		uint8_t* eL = bypass ? &m_dto.vspBpExhaustL : &m_dto.vspExhaustL;
		uint8_t* eM = bypass ? &m_dto.vspBpExhaustM : &m_dto.vspExhaustM;
		uint8_t* eH = bypass ? &m_dto.vspBpExhaustH : &m_dto.vspExhaustH;
		uint8_t* sL = bypass ? &m_dto.vspBpSupplyL : &m_dto.vspSupplyL;
		uint8_t* sM = bypass ? &m_dto.vspBpSupplyM : &m_dto.vspSupplyM;
		uint8_t* sH = bypass ? &m_dto.vspBpSupplyH : &m_dto.vspSupplyH;
		if( *eH != u[0] || *eM != u[1] || *eL != u[2] ) {
			*eH = u[0]; *eM = u[1]; *eL = u[2];
			if( RC_SEND_C5_ON_VSP_CHANGE ) m_vspPending |= bypass ? VSP_PEND_BP_EXHAUST : VSP_PEND_EXHAUST;
		}
		if( *sH != u[3] || *sM != u[4] || *sL != u[5] ) {
			*sH = u[3]; *sM = u[4]; *sL = u[5];
			if( RC_SEND_C5_ON_VSP_CHANGE ) m_vspPending |= bypass ? VSP_PEND_BP_SUPPLY : VSP_PEND_SUPPLY;
		}
		m_nvsDirty = true;
		m_lastChangeMs = millis();
	}
	notifyUi();
	return true;
}

bool RcController::setFilterReset() {
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		if( RC_SEND_FILTER_FRAME ) m_filterPending = true;
		m_dto.filterUsedMin = 0;
		m_filterRunMs = 0;
	}
	saveFilterMinutes(0);
	LOGI("Filter reset requested");
	notifyUi();
	return true;
}

bool RcController::setFilterInitial(int x100) {
	if( RC_FILTER_INIT_FIXED ) x100 = RC_FILTER_INIT_DEFAULT_X100;
	if( x100 < RC_FILTER_INIT_MIN || x100 > RC_FILTER_INIT_MAX ) {
		LOGE("Filter initial out of range: %d", x100);
		return false;
	}
	{
		Guard g(m_mutex);
		if( lockedOut(false) ) return false;
		m_dto.filterInit = (uint8_t)x100;
		if( RC_SEND_FILTER_FRAME ) m_filterPending = true;
		m_nvsDirty = true;
		m_lastChangeMs = millis();
		// 초기값 전송 = 필터 초기화 (MAIN 도 필터시간을 초기값으로)
		m_dto.filterUsedMin = 0;
		m_filterRunMs = 0;
	}
	saveFilterMinutes(0);
	LOGI("Filter initial set: %d (%d h), filter time reset", x100, x100 * 100);
	notifyUi();
	return true;
}

bool RcController::setFunctionState(const std::string& func, bool on) {
	{
		Guard g(m_mutex);
		const bool unlocking = (func == "lock" && !on);
		if( lockedOut(unlocking) ) return false;

		bool funcFrame = false;
		if( func == "exhaust" ) {
			m_dto.exhaust = on;
			funcFrame = true;
		} else if( func == "supply" ) {
			m_dto.supply = on;
			funcFrame = true;
		} else if( func == "sleep" || func == "night" ) {
			m_dto.night = on;
			funcFrame = true;
		} else if( func == "dust" ) {
			m_dto.dust = on;
			funcFrame = true;
		} else if( func == "heater" ) {
			m_dto.heater = on;
		} else if( func == "clean" ) {
			if( on ) {
				m_dto.applyUiMode(RC::UI_CLEAN);
			} else if( m_dto.clean ) {
				m_dto.applyUiMode(RC::UI_VENTILATION);
			}
		} else if( func == "lock" ) {
			m_dto.ahuLock = on;
		} else {
			return false;
		}
		if( funcFrame && RC_SEND_FUNCTION_FRAME ) {
			m_funcPending = true;
		}
		markLocalChange();
	}
	notifyUi();
	return true;
}

void RcController::setDataCallback(DataCallback callback) {
	Guard g(m_mutex);
	m_callback = callback;
}

void RcController::setLocalSensors(const RC::SensorCache& sensor) {
	bool changed;
	{
		Guard g(m_mutex);
		m_sensor = sensor;
		changed = evaluateAuto();
	}
	if( changed ) {
		notifyUi();
	}
}

// 구 RC 2.4.5 / Table 4: 자동 모드에서 RC 가 풍량, 환기/청정 결정
bool RcController::evaluateAuto() {
	if( !m_dto.autoActive() ) {
		m_autoValid = false;
		return false;
	}
	const RC::AutoResult r = RC::autoDecide(m_sensor.hasCo2, m_sensor.co2,
		m_sensor.hasPm, m_sensor.pm25, m_sensor.pm10);
	if( m_autoValid && r.volume == m_dto.autoVolume && r.clean == m_dto.autoClean ) {
		return false;
	}
	const uint32_t now = millis();
	if( m_autoValid && now - m_autoChangedMs < RC_AUTO_MIN_HOLD_MS ) {
		return false;
	}
	m_dto.autoVolume = r.volume;
	m_dto.autoClean = r.clean;
	m_autoValid = true;
	m_autoChangedMs = now;
	m_ctrlPending = RC_CTRL_FLAG_ON_LOCAL_CHANGE;	// RC 가 바꾼 제어값
	LOGI("Auto: fan=%u %s (co2=%d pm2.5=%d pm10=%d)", (unsigned)r.volume, r.clean ? "clean" : "vent",
		m_sensor.hasCo2 ? m_sensor.co2 : -1, m_sensor.hasPm ? m_sensor.pm25 : -1,
		m_sensor.hasPm ? m_sensor.pm10 : -1);
	return true;
}

RcDto RcController::getState() {
	Guard g(m_mutex);
	return m_dto;
}

RC::SensorCache RcController::getSensors() {
	Guard g(m_mutex);
	return m_sensor;
}

void RcController::notifyUi() {
	m_refreshPending = true;	// UI 태스크가 uiTick() 에서 한 번만 처리 (중복 합침)
}

void RcController::dispatchUi() {
	RcDto copy;
	DataCallback cb;
	{
		Guard g(m_mutex);
		copy = m_dto;
		cb = m_callback;
	}
	if( cb ) {
		cb(copy);
	}
}

void RcController::uiTick() {
	const bool lost = plc_protocol::plcMsSinceLastFrame() > RC_LINK_TIMEOUT_MS;
	if( lost != m_linkLost ) {
		m_linkLost = lost;
		{
			Guard g(m_mutex);
			m_dto.linkLost = lost;
		}
		if( lost ) {
			LOGW("MAIN link lost (> %d ms)", RC_LINK_TIMEOUT_MS);
		} else {
			LOGI("MAIN link restored");
		}
		notifyUi();
	}

	if( RC_FILTER_LOCAL_COUNT ) countFilterTime();

	if( m_refreshPending.exchange(false) ) {
		dispatchUi();
	}

	bool save = false;
	RcDto copy;
	{
		Guard g(m_mutex);
		if( m_nvsDirty && millis() - m_lastChangeMs >= RC_NVS_SAVE_DELAY_MS ) {
			m_nvsDirty = false;
			copy = m_dto;
			save = true;
		}
	}
	if( save ) {
		saveSettings(copy);
	}
}

// RC 자체 필터 사용시간: 앱 루프마다 경과 시간을 더하고 1분마다 반영,
// RC_FILTER_SAVE_EVERY_MIN 분마다 NVS 저장
void RcController::countFilterTime() {
	const uint32_t now = millis();
	const uint32_t dt = m_filterTickMs ? now - m_filterTickMs : 0;
	m_filterTickMs = now;

	uint32_t saveMin = 0;
	bool minuteDone = false;
	{
		Guard g(m_mutex);
		if( RC_FILTER_COUNT_RUNNING_ONLY && !m_dto.isRunning() ) return;
		m_filterRunMs += dt;
		if( m_filterRunMs < 60000 ) return;
		m_filterRunMs -= 60000;
		m_dto.filterUsedMin++;
		minuteDone = true;
		if( m_dto.filterUsedMin % RC_FILTER_SAVE_EVERY_MIN == 0 ) saveMin = m_dto.filterUsedMin;
	}
	if( saveMin ) saveFilterMinutes(saveMin);
	if( minuteDone ) notifyUi();
}

void RcController::saveFilterMinutes(uint32_t minutes) {
	nvs_handle_t h;
	if( nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK ) return;
	nvs_set_u32(h, "filt_min", minutes);
	nvs_commit(h);
	nvs_close(h);
}

uint32_t RcController::loadFilterMinutes() {
	nvs_handle_t h;
	uint32_t minutes = 0;
	if( nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK ) return 0;
	nvs_get_u32(h, "filt_min", &minutes);
	nvs_close(h);
	return minutes;
}

void RcController::handleFrame(const uint8_t frame[6]) {
	LOGI("%02X RX: %s", frame[0], PlcService::toHex(frame, PLC::FRAME_LEN).c_str());

	bool changed = false;
	{
		Guard g(m_mutex);
		switch( frame[0] ) {
			case PLC::D5:
			case PLC::E5:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::d5e5b5>(frame));
				break;
			case PLC::D6:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::d6b6>(frame));
				break;
			case PLC::D7:
			case PLC::E7:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::d7e7b7>(frame));
				break;
			case PLC::D8:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::d8>(frame));
				break;
			case PLC::D9:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::d9>(frame));
				break;
			case PLC::A6:
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::a6>(frame));
				break;
			default:
				return;
		}
		if( changed ) {
			m_nvsDirty = true;
			m_lastChangeMs = millis();
			evaluateAuto();	// MAIN 이 자동 진입/해제했을 수 있음
		}
	}
	notifyUi();
}

bool RcController::buildReply(uint8_t header, uint8_t out[6]) {
	Guard g(m_mutex);

	// B5 일반 응답 + 예약 모드가 실리면 시간 프레임 준비
	auto normalB5 = [&](bool ctrl) {
		const PLC::d5e5b5 packet = m_dto.makeB5(ctrl);
		if( m_resvState == RESV_ARMED &&
			(packet.data2 == PLC::B5_RUN_RSV || packet.data2 == PLC::B5_STOP_RSV) ) {
			m_resvState = RESV_READY;
		}
		PlcService::toBytes(packet, out);
	};

	switch( header ) {
		case PLC::D5: {
			// 우선순위: C5 (VSP) -> 예약 시간 B5 -> 필터 B5 -> Function B5 -> 일반 B5
			if( m_vspPending & VSP_PEND_EXHAUST ) {
				m_vspPending &= ~VSP_PEND_EXHAUST;
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_EXHAUST_HEAT_EXCH,
					m_dto.vspExhaustL, m_dto.vspExhaustM, m_dto.vspExhaustH), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_SUPPLY ) {
				m_vspPending &= ~VSP_PEND_SUPPLY;
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_SUPPLY_HEAT_EXCH,
					m_dto.vspSupplyL, m_dto.vspSupplyM, m_dto.vspSupplyH), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_BP_EXHAUST ) {
				m_vspPending &= ~VSP_PEND_BP_EXHAUST;
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_EXHAUST_BYPASS,
					m_dto.vspBpExhaustL, m_dto.vspBpExhaustM, m_dto.vspBpExhaustH), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_BP_SUPPLY ) {
				m_vspPending &= ~VSP_PEND_BP_SUPPLY;
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_SUPPLY_BYPASS,
					m_dto.vspBpSupplyL, m_dto.vspBpSupplyM, m_dto.vspBpSupplyH), out);
				return true;
			}
			if( m_resvState == RESV_READY ) {
				m_resvState = RESV_NONE;
				const uint16_t minutes = (m_resvSel == PLC::B5_RUN_RSV_TM) ? m_dto.startMin : m_dto.stopMin;
				PlcService::toBytes(PlcService::makeB5Time(m_dto, m_resvSel, minutes,
					RC_RESERVE_TIME_CTRL_FLAG), out);
				return true;
			}
			if( m_filterPending ) {
				m_filterPending = false;
				PlcService::toBytes(PlcService::makeB5Filter(m_dto, true), out);
				return true;
			}
			if( m_funcPending ) {
				m_funcPending = false;
				PlcService::toBytes(PlcService::makeB5Function(m_dto, RC_FUNCTION_FRAME_CTRL_FLAG), out);
				return true;
			}
			normalB5(m_ctrlPending);
			m_ctrlPending = false;
			return true;
		}
		case PLC::E5: {
			// E5 반영 후 상태 -> 새 상태를 보고
			const bool ctrl = RC_CTRL_FLAG_IN_E5_REPLY && m_ctrlPending;
			normalB5(ctrl);
			if( ctrl ) {
				m_ctrlPending = false;
			}
			return true;
		}
		case PLC::D6:
			if( !RC_REPLY_TO_D6 ) return false;
			if( RC_D6_REPLY_HEADER == PLC::B8 ) {
				PlcService::toBytes(PlcService::makeB8(m_sensor), out);
			} else {
				PlcService::toBytes(PlcService::makeB6(m_sensor), out);
			}
			return true;
		case PLC::D7:
		case PLC::E7:
			if( !RC_REPLY_TO_D7 ) return false;
			PlcService::toBytes(PlcService::makeB7(m_dto, m_sensor), out);
			return true;
		default:
			return false;	// D8, D9, A6: 응답 없음
	}
}

void RcController::saveSettings(const RcDto& dto) {
	SettingsBlob blob;
	blob.version = NVS_VERSION;
	blob.mode = dto.mode;
	blob.volume = dto.volume;
	blob.flags = (uint8_t)(
		(dto.bypass    ? FB_BYPASS  : 0) |
		(dto.heater    ? FB_HEATER  : 0) |
		(dto.clean     ? FB_CLEAN   : 0) |
		(dto.autoDrive ? FB_AUTO    : 0) |
		(dto.ahuLock   ? FB_LOCK    : 0) |
		(dto.exhaust   ? FB_EXHAUST : 0) |
		(dto.supply    ? FB_SUPPLY  : 0) |
		(dto.night     ? FB_NIGHT   : 0));
	blob.flags2 = (uint8_t)((dto.dust ? FB2_DUST : 0) | (dto.aiMode ? FB2_AI : 0));
	blob.deviceId = dto.deviceId;
	blob.startMinLo = (uint8_t)(dto.startMin & 0xFF);
	blob.startMinHi = (uint8_t)(dto.startMin >> 8);
	blob.stopMinLo = (uint8_t)(dto.stopMin & 0xFF);
	blob.stopMinHi = (uint8_t)(dto.stopMin >> 8);
	blob.filterInit = dto.filterInit;
	blob.vsp[0] = dto.vspExhaustL;
	blob.vsp[1] = dto.vspExhaustM;
	blob.vsp[2] = dto.vspExhaustH;
	blob.vsp[3] = dto.vspSupplyL;
	blob.vsp[4] = dto.vspSupplyM;
	blob.vsp[5] = dto.vspSupplyH;
	blob.vsp[6] = dto.vspBpExhaustL;
	blob.vsp[7] = dto.vspBpExhaustM;
	blob.vsp[8] = dto.vspBpExhaustH;
	blob.vsp[9] = dto.vspBpSupplyL;
	blob.vsp[10] = dto.vspBpSupplyM;
	blob.vsp[11] = dto.vspBpSupplyH;

	nvs_handle_t h;
	if( nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK ) {
		LOGW("NVS open failed");
		return;
	}
	esp_err_t err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
	if( err == ESP_OK ) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	LOGI("RC settings saved: %s", err == ESP_OK ? "ok" : esp_err_to_name(err));
}

bool RcController::loadSettings(RcDto& dto) {
	nvs_handle_t h;
	if( nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK ) {
		return false;
	}
	SettingsBlob blob;
	size_t len = sizeof(blob);
	const esp_err_t err = nvs_get_blob(h, NVS_KEY, &blob, &len);
	nvs_close(h);
	if( err != ESP_OK || len != sizeof(blob) || blob.version != NVS_VERSION ) {
		return false;
	}
	const uint16_t startMin = (uint16_t)(blob.startMinLo | (blob.startMinHi << 8));
	const uint16_t stopMin = (uint16_t)(blob.stopMinLo | (blob.stopMinHi << 8));
	if( blob.mode > RC::STOP_RESERVE || blob.volume > RC::VOLUME_MAX ||
		startMin > RC_RESERVE_MAX_MIN || stopMin > RC_RESERVE_MAX_MIN ||
		blob.filterInit < RC_FILTER_INIT_MIN || blob.filterInit > RC_FILTER_INIT_MAX ||
		blob.deviceId > DEVICE_ID_MAX ) {
		return false;
	}
	for( int i = 0 ; i < 12 ; i++ ) {
		if( !vspValid(blob.vsp[i]) ) return false;
	}

	dto.mode = blob.mode;
	dto.volume = blob.volume;
	dto.bypass = blob.flags & FB_BYPASS;
	dto.heater = blob.flags & FB_HEATER;
	dto.clean = blob.flags & FB_CLEAN;
	dto.autoDrive = blob.flags & FB_AUTO;
	dto.ahuLock = blob.flags & FB_LOCK;
	dto.exhaust = blob.flags & FB_EXHAUST;
	dto.supply = blob.flags & FB_SUPPLY;
	dto.night = blob.flags & FB_NIGHT;
	dto.dust = blob.flags2 & FB2_DUST;
	dto.aiMode = blob.flags2 & FB2_AI;
	dto.deviceId = blob.deviceId;
	dto.startMin = startMin;
	dto.stopMin = stopMin;
	dto.filterInit = RC_FILTER_INIT_FIXED ? RC_FILTER_INIT_DEFAULT_X100 : blob.filterInit;
	dto.vspExhaustL = blob.vsp[0];
	dto.vspExhaustM = blob.vsp[1];
	dto.vspExhaustH = blob.vsp[2];
	dto.vspSupplyL = blob.vsp[3];
	dto.vspSupplyM = blob.vsp[4];
	dto.vspSupplyH = blob.vsp[5];
	dto.vspBpExhaustL = blob.vsp[6];
	dto.vspBpExhaustM = blob.vsp[7];
	dto.vspBpExhaustH = blob.vsp[8];
	dto.vspBpSupplyL = blob.vsp[9];
	dto.vspBpSupplyM = blob.vsp[10];
	dto.vspBpSupplyH = blob.vsp[11];
	return true;
}

void onPlcFrame(const uint8_t frame[6]) {
	RcController::instance().handleFrame(frame);
}

bool onPlcReplyDue(uint8_t header, uint8_t out[6]) {
	return RcController::instance().buildReply(header, out);
}
