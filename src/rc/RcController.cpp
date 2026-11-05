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

// m_vspSkew
enum VspSkew {
	SKEW_NONE = 0, SKEW_HEAT = 1, SKEW_BYPASS = 2
};

enum FlagBit2 {
	FB2_DUST = 0x01, FB2_AI = 0x02, FB2_POSITIVE = 0x04, FB2_NEGATIVE = 0x08, FB2_VSP_SKEWED = 0x10,
	FB2_VSP_SKEWED_BP = 0x20
};

// 양압 / 음압 설정점 (NVS "press", 설정 blob 과 별도 - 기존 blob 형식 유지)
const char* NVS_PRESS_KEY = "press";
const uint8_t NVS_PRESS_VERSION = 1;

typedef struct {
	uint8_t valid;
	uint8_t room;
	uint8_t count;
	uint8_t offsetPct;
	uint8_t capped;
	uint8_t supplyLo, supplyHi;
	uint8_t exhaustLo, exhaustHi;
	uint8_t neededLo, neededHi;
} PressBlobSetup;

typedef struct {
	uint8_t version;
	uint8_t gasWarned;
	PressBlobSetup pos;
	PressBlobSetup neg;
} PressBlob;

void packSetup(const RC::PressureSetup& p, PressBlobSetup& b) {
	b.valid = p.valid;
	b.room = p.room;
	b.count = p.count;
	b.offsetPct = p.offsetPct;
	b.capped = p.capped;
	b.supplyLo = (uint8_t)(p.supply & 0xFF);
	b.supplyHi = (uint8_t)(p.supply >> 8);
	b.exhaustLo = (uint8_t)(p.exhaust & 0xFF);
	b.exhaustHi = (uint8_t)(p.exhaust >> 8);
	b.neededLo = (uint8_t)(p.needed & 0xFF);
	b.neededHi = (uint8_t)(p.needed >> 8);
}

RC::PressureSetup unpackSetup(const PressBlobSetup& b, uint8_t kind) {
	RC::PressureSetup p = {};
	int n = 0;
	RC::pressureRooms(kind, n);
	p.supply = (uint16_t)(b.supplyLo | (b.supplyHi << 8));
	p.exhaust = (uint16_t)(b.exhaustLo | (b.exhaustHi << 8));
	p.needed = (uint16_t)(b.neededLo | (b.neededHi << 8));
	p.room = b.room;
	p.count = b.count;
	p.offsetPct = b.offsetPct;
	p.capped = b.capped != 0;
	// 저장값 검증: 방향이 맞고 정격 이하일 때만 사용
	const bool dirOk = (kind == RC::PRESS_POSITIVE) ? p.supply > p.exhaust : p.exhaust > p.supply;
	p.valid = b.valid && b.room < n && dirOk && p.supply <= RC_FAN_RATED_M3H && p.exhaust <= RC_FAN_RATED_M3H;
	if( !p.valid ) p = RC::PressureSetup();
	return p;
}

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
	  m_vspSkew(SKEW_NONE),
	  m_co2HighMs(0),
	  m_co2LowMs(0),
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
	uint8_t skewed = SKEW_NONE;
	const bool ok = (err == ESP_OK) && loadSettings(loaded, skewed);
	if( !ok ) {
		loaded.setDefaults();
		skewed = SKEW_NONE;
	}
	loaded.filterUsedMin = (err == ESP_OK) ? loadFilterMinutes() : 0;
	if( loaded.pressure != RC::PRESS_NONE && !loaded.pressureActive() ) loaded.pressure = RC::PRESS_NONE;
	{
		Guard g(m_mutex);
		m_dto = loaded;
		m_vspSkew = skewed;
		// 정전 후: 양압/음압이면 그 VSP 표를 다시 보내고, 아니면서 MAIN 에 양압/음압 표가
		// 남아 있으면 원래 표를 보냄
		if( m_dto.pressureActive() || m_vspSkew != SKEW_NONE ) syncPressureVsp(true);
	}
	if( loaded.pressureActive() ) {
		const RC::PressureSetup* p = loaded.pressureSetup(loaded.pressure);
		LOGI("Resume %s%s mode: supply %u exhaust %u m3/h", loaded.bypass ? "bypass " : "",
			loaded.pressure == RC::PRESS_POSITIVE ? "positive" : "negative",
			(unsigned)p->supply, (unsigned)p->exhaust);
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
	else if( mode == "positive" ) m = RC::UI_POSITIVE;
	else if( mode == "negative" ) m = RC::UI_NEGATIVE;
	else if( mode == "bypass_positive" ) m = RC::UI_BP_POSITIVE;
	else if( mode == "bypass_negative" ) m = RC::UI_BP_NEGATIVE;
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
		if( m == RC::UI_POSITIVE || m == RC::UI_NEGATIVE || m == RC::UI_BP_POSITIVE || m == RC::UI_BP_NEGATIVE ) {
			const bool pos = (m == RC::UI_POSITIVE || m == RC::UI_BP_POSITIVE);
			const RC::PressureSetup* p = m_dto.pressureSetup(pos ? RC::PRESS_POSITIVE : RC::PRESS_NEGATIVE);
			if( !p || !p->valid ) {
				LOGW("Mode change ignored: %s not set up", mode.c_str());
				return false;
			}
		}
		const bool exh = m_dto.exhaust, sup = m_dto.supply;
		m_dto.applyUiMode(m);	// 모드 비트만 변경, 풍량/전원/잠금/히터 유지
		if( (exh != m_dto.exhaust || sup != m_dto.supply) && RC_SEND_FUNCTION_FRAME ) m_funcPending = true;
		markLocalChange();
		evaluateAuto();
		syncPressureVsp(true);	// 양압/음압 표 전송, 또는 다른 모드면 원래 표로 복귀
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
		if( m_dto.autoActive() || m_dto.night || m_dto.pressureActive() ) {
			LOGW("Fan change ignored: %s", m_dto.night ? "sleep" : (m_dto.pressureActive() ? "pressure" : "auto"));
			return false;
		}
		m_dto.setVolume((uint8_t)v);	// 풍량만 변경, 모드 유지
		markLocalChange();
	}
	notifyUi();
	return true;
}

bool RcController::applyPressureSetup(uint8_t kind, uint8_t room, uint8_t count, bool bypassPath,
				RC::PressureSetup* out) {
	const RC::PressureSetup r = RC::pressureCompute(kind, room, count);
	if( out ) *out = r;
	if( !r.valid ) {
		LOGE("Pressure setup invalid: kind=%u room=%u", (unsigned)kind, (unsigned)room);
		return false;
	}
	bool started = false;
	{
		Guard g(m_mutex);
		if( kind == RC::PRESS_POSITIVE ) m_dto.pressPos = r;
		else m_dto.pressNeg = r;
		m_nvsDirty = true;
		m_lastChangeMs = millis();
		if( !lockedOut(false) && m_dto.isRunning() ) {
			const bool exh = m_dto.exhaust, sup = m_dto.supply;
			const bool pos = kind == RC::PRESS_POSITIVE;
			m_dto.applyUiMode(bypassPath ? (pos ? RC::UI_BP_POSITIVE : RC::UI_BP_NEGATIVE)
										: (pos ? RC::UI_POSITIVE : RC::UI_NEGATIVE));
			if( (exh != m_dto.exhaust || sup != m_dto.supply) && RC_SEND_FUNCTION_FRAME ) m_funcPending = true;
			markLocalChange();
			evaluateAuto();
			started = true;
		}
		syncPressureVsp(true);	// 이미 그 모드로 운전 중이면 새 설정점 전송
	}
	LOGI("Pressure setup %s: supply %u exhaust %u m3/h (%c%u%%)%s%s",
		kind == RC::PRESS_POSITIVE ? "positive" : "negative", (unsigned)r.supply, (unsigned)r.exhaust,
		kind == RC::PRESS_POSITIVE ? '+' : '-', (unsigned)r.offsetPct,
		r.capped ? " capped to rated" : "", started ? ", started" : ", saved only");
	notifyUi();
	return started;
}

void RcController::markGasWarned() {
	{
		Guard g(m_mutex);
		if( m_dto.gasWarned ) return;
		m_dto.gasWarned = true;
		m_nvsDirty = true;
		m_lastChangeMs = millis();
	}
	notifyUi();
}

// MAIN 의 VSP 표를 맞춤: 양압/음압이면 그 경로 (전열 / 바이패스) 세트에 양압/음압 표,
// 다른 세트는 원래 표 (C5, 팬 1개당 1프레임).
// force: 양압/음압 표를 다시 보냄 (진입, 설정점 변경, 부팅)
void RcController::syncPressureVsp(bool force) {
	const uint8_t want = !m_dto.pressureActive() ? SKEW_NONE : (m_dto.bypass ? SKEW_BYPASS : SKEW_HEAT);
	if( want == SKEW_NONE ) {
		m_dto.pressBoost = false;
		m_co2HighMs = 0;
		m_co2LowMs = 0;
	}
	if( want == m_vspSkew && !(force && want != SKEW_NONE) ) return;
	if( RC_SEND_C5_ON_VSP_CHANGE ) {
		// 원래 표로 되돌릴 세트 + 양압/음압 표를 보낼 세트
		const uint8_t sets[2] = { m_vspSkew, want };
		for( uint8_t set : sets ) {
			if( set == SKEW_HEAT ) m_vspPending |= VSP_PEND_EXHAUST | VSP_PEND_SUPPLY;
			if( set == SKEW_BYPASS ) m_vspPending |= VSP_PEND_BP_EXHAUST | VSP_PEND_BP_SUPPLY;
		}
	}
	if( want != m_vspSkew ) {
		static const char* names[3] = { "normal", "pressure on heat-exchange set", "pressure on bypass set" };
		m_vspSkew = want;
		m_nvsDirty = true;
		m_lastChangeMs = millis();
		LOGI("VSP table -> %s", names[want]);
	}
}

// 양압/음압 CO2 부스트: > HIGH ppm 이 HIGH_MS 계속 -> 부스트, < LOW ppm 이 LOW_MS 계속 -> 해제
bool RcController::evaluatePressureBoost() {
	if( !RC_PRESS_CO2_BOOST || !m_dto.pressureActive() || !m_sensor.hasCo2 ) {
		m_co2HighMs = 0;
		m_co2LowMs = 0;
		if( m_dto.pressBoost ) {
			m_dto.pressBoost = false;
			m_ctrlPending = RC_CTRL_FLAG_ON_LOCAL_CHANGE;
			return true;
		}
		return false;
	}
	const uint32_t now = millis() | 1;	// 0 = 타이머 없음
	const int co2 = m_sensor.co2;
	if( !m_dto.pressBoost ) {
		m_co2LowMs = 0;
		if( co2 <= RC_PRESS_CO2_HIGH_PPM ) { m_co2HighMs = 0; return false; }
		if( !m_co2HighMs ) m_co2HighMs = now;
		if( now - m_co2HighMs < RC_PRESS_CO2_HIGH_MS ) return false;
		m_dto.pressBoost = true;
	} else {
		m_co2HighMs = 0;
		if( co2 >= RC_PRESS_CO2_LOW_PPM ) { m_co2LowMs = 0; return false; }
		if( !m_co2LowMs ) m_co2LowMs = now;
		if( now - m_co2LowMs < RC_PRESS_CO2_LOW_MS ) return false;
		m_dto.pressBoost = false;
	}
	m_co2HighMs = 0;
	m_co2LowMs = 0;
	m_ctrlPending = RC_CTRL_FLAG_ON_LOCAL_CHANGE;	// 풍량 단계 1 <-> 2
	LOGI("Pressure CO2 boost %s (co2=%d)", m_dto.pressBoost ? "on" : "off", co2);
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
			// 양압/음압 중에는 MAIN 의 그 세트에 양압/음압 표가 있음 - 원래 표는 모드 해제 때 전송
			if( RC_SEND_C5_ON_VSP_CHANGE && m_vspSkew != (bypass ? SKEW_BYPASS : SKEW_HEAT) ) m_vspPending |= bypass ? VSP_PEND_BP_EXHAUST : VSP_PEND_EXHAUST;
		}
		if( *sH != u[3] || *sM != u[4] || *sL != u[5] ) {
			*sH = u[3]; *sM = u[4]; *sL = u[5];
			if( RC_SEND_C5_ON_VSP_CHANGE && m_vspSkew != (bypass ? SKEW_BYPASS : SKEW_HEAT) ) m_vspPending |= bypass ? VSP_PEND_BP_SUPPLY : VSP_PEND_SUPPLY;
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
		m_dto.dropRcModesIfOverridden();	// 급기/배기 단독 -> 양압/음압 해제
		syncPressureVsp(false);
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
		changed = evaluatePressureBoost() || changed;
	}
	if( changed ) {
		notifyUi();
	}
}

// 구 RC 2.4.5 / Table 4: 자동 모드에서 RC 가 풍량, 전열 /청정 결정
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
	uint8_t skewed = SKEW_NONE;
	RcDto copy;
	{
		Guard g(m_mutex);
		if( m_nvsDirty && millis() - m_lastChangeMs >= RC_NVS_SAVE_DELAY_MS ) {
			m_nvsDirty = false;
			copy = m_dto;
			skewed = m_vspSkew;
			save = true;
		}
	}
	if( save ) {
		saveSettings(copy, skewed);
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
				// 양압/음압 표를 보낸 동안 MAIN 이 되돌려 주는 그 세트의 VSP 는 무시 (원래 표 보존)
				if( RC_PRESS_IGNORE_A6_WHILE_SKEWED &&
					((m_vspSkew == SKEW_HEAT &&
					  (frame[1] == PLC::VSP_EXHAUST_HEAT_EXCH || frame[1] == PLC::VSP_SUPPLY_HEAT_EXCH)) ||
					 (m_vspSkew == SKEW_BYPASS &&
					  (frame[1] == PLC::VSP_EXHAUST_BYPASS || frame[1] == PLC::VSP_SUPPLY_BYPASS))) ) {
					m_dto.rxMask |= RC::RX_A6;
					return;
				}
				changed = m_dto.structConverter(PlcService::fromBytes<PLC::a6>(frame));
				break;
			default:
				return;
		}
		if( changed ) {
			m_nvsDirty = true;
			m_lastChangeMs = millis();
			evaluateAuto();	// MAIN 이 자동 진입/해제했을 수 있음
			syncPressureVsp(false);	// MAIN 이 모드를 바꿔 양압/음압이 해제됐을 수 있음
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
			// 전열 표: 양압/음압이면 그 설정점 표 (약 = 설정점, 중/강 = 부스트), 아니면 원래 표
			if( m_vspPending & VSP_PEND_EXHAUST ) {
				m_vspPending &= ~VSP_PEND_EXHAUST;
				uint8_t l = m_dto.vspExhaustL, m = m_dto.vspExhaustM, h = m_dto.vspExhaustH;
				if( m_vspSkew == SKEW_HEAT ) m_dto.pressureVspTable(false, l, m, h);
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_EXHAUST_HEAT_EXCH, l, m, h), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_SUPPLY ) {
				m_vspPending &= ~VSP_PEND_SUPPLY;
				uint8_t l = m_dto.vspSupplyL, m = m_dto.vspSupplyM, h = m_dto.vspSupplyH;
				if( m_vspSkew == SKEW_HEAT ) m_dto.pressureVspTable(true, l, m, h);
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_SUPPLY_HEAT_EXCH, l, m, h), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_BP_EXHAUST ) {
				m_vspPending &= ~VSP_PEND_BP_EXHAUST;
				uint8_t l = m_dto.vspBpExhaustL, m = m_dto.vspBpExhaustM, h = m_dto.vspBpExhaustH;
				if( m_vspSkew == SKEW_BYPASS ) m_dto.pressureVspTable(false, l, m, h);
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_EXHAUST_BYPASS, l, m, h), out);
				return true;
			}
			if( m_vspPending & VSP_PEND_BP_SUPPLY ) {
				m_vspPending &= ~VSP_PEND_BP_SUPPLY;
				uint8_t l = m_dto.vspBpSupplyL, m = m_dto.vspBpSupplyM, h = m_dto.vspBpSupplyH;
				if( m_vspSkew == SKEW_BYPASS ) m_dto.pressureVspTable(true, l, m, h);
				PlcService::toBytes(PlcService::makeC5(PLC::C5_VSP_SUPPLY_BYPASS, l, m, h), out);
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

void RcController::saveSettings(const RcDto& dto, uint8_t vspSkew) {
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
	blob.flags2 = (uint8_t)((dto.dust ? FB2_DUST : 0) | (dto.aiMode ? FB2_AI : 0) |
		(dto.pressure == RC::PRESS_POSITIVE ? FB2_POSITIVE : 0) |
		(dto.pressure == RC::PRESS_NEGATIVE ? FB2_NEGATIVE : 0) |
		(vspSkew == SKEW_HEAT ? FB2_VSP_SKEWED : 0) |
		(vspSkew == SKEW_BYPASS ? FB2_VSP_SKEWED_BP : 0));
	PressBlob press = {};
	press.version = NVS_PRESS_VERSION;
	press.gasWarned = dto.gasWarned;
	packSetup(dto.pressPos, press.pos);
	packSetup(dto.pressNeg, press.neg);
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
		err = nvs_set_blob(h, NVS_PRESS_KEY, &press, sizeof(press));
	}
	if( err == ESP_OK ) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	LOGI("RC settings saved: %s", err == ESP_OK ? "ok" : esp_err_to_name(err));
}

bool RcController::loadSettings(RcDto& dto, uint8_t& vspSkew) {
	nvs_handle_t h;
	if( nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK ) {
		return false;
	}
	SettingsBlob blob;
	size_t len = sizeof(blob);
	const esp_err_t err = nvs_get_blob(h, NVS_KEY, &blob, &len);
	PressBlob press = {};
	size_t plen = sizeof(press);
	const bool pressOk = nvs_get_blob(h, NVS_PRESS_KEY, &press, &plen) == ESP_OK &&
		plen == sizeof(press) && press.version == NVS_PRESS_VERSION;
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
	if( pressOk ) {
		dto.pressPos = unpackSetup(press.pos, RC::PRESS_POSITIVE);
		dto.pressNeg = unpackSetup(press.neg, RC::PRESS_NEGATIVE);
		dto.gasWarned = press.gasWarned != 0;
	}
	dto.pressure = (blob.flags2 & FB2_POSITIVE) ? RC::PRESS_POSITIVE :
		((blob.flags2 & FB2_NEGATIVE) ? RC::PRESS_NEGATIVE : RC::PRESS_NONE);
	vspSkew = (blob.flags2 & FB2_VSP_SKEWED) ? SKEW_HEAT :
		((blob.flags2 & FB2_VSP_SKEWED_BP) ? SKEW_BYPASS : SKEW_NONE);
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
