#include "WifiController.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "io/sensors.h"
#include "port/esp_time.h"
#include "WifiPortal.h"
#include "WifiService.h"

namespace {

const char* TAG = "WIFI";

const uint32_t kRetrySteps[] = WIFI_RETRY_STEPS_MS;
const uint8_t kRetryCount = sizeof(kRetrySteps) / sizeof(kRetrySteps[0]);
const uint32_t kAttemptTimeoutMs = 20000;	// 이벤트 없이 끝난 시도 정리

class Guard {
public:
	explicit Guard(SemaphoreHandle_t m) : m_m(m) { xSemaphoreTake(m_m, portMAX_DELAY); }
	~Guard() { xSemaphoreGive(m_m); }
private:
	SemaphoreHandle_t m_m;
};

void copyStr(char* dst, size_t dstLen, const char* src) {
	strncpy(dst, src, dstLen - 1);
	dst[dstLen - 1] = '\0';
}

WIFI::Fail failFromReason(uint8_t reason) {
	switch( reason ) {
		case WIFI_REASON_AUTH_FAIL:
		case WIFI_REASON_AUTH_EXPIRE:
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
		case WIFI_REASON_MIC_FAILURE:
			return WIFI::FAIL_AUTH;
		case WIFI_REASON_NO_AP_FOUND:
			return WIFI::FAIL_NO_AP;
		default:
			return WIFI::FAIL_OTHER;
	}
}

} // namespace

WifiController& WifiController::instance() {
	static WifiController s_instance;
	return s_instance;
}

WifiController::WifiController()
	: m_mutex(xSemaphoreCreateMutex()),
	  m_started(false),
	  m_trial(false),
	  m_tries(0),
	  m_retryIdx(0),
	  m_attempt(false),
	  m_attemptMs(0),
	  m_assoc(false),
	  m_restart(false),
	  m_expectDisc(false),
	  m_nextTryMs(0),
	  m_scanReq(false),
	  m_scanRetryMs(0),
	  m_setupReq(false),
	  m_setupStopReq(false),
	  m_forgetReq(false),
	  m_setupStartMs(0),
	  m_setupDoneMs(0),
	  m_rssiMs(0),
	  m_timeSyncPending(false),
	  m_refreshPending(false) {
	m_savedSsid[0] = m_savedPass[0] = m_wantSsid[0] = m_wantPass[0] = '\0';
}

void WifiController::init() {
	uint8_t mac[6];
	WifiService::readMac(mac);
	{
		Guard g(m_mutex);
		snprintf(m_dto.mac, sizeof(m_dto.mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
				 mac[4], mac[5]);
		snprintf(m_dto.apSsid, sizeof(m_dto.apSsid), "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
		snprintf(m_dto.apPass, sizeof(m_dto.apPass), "%s%02x%02x", WIFI_AP_PASS_PREFIX, mac[4], mac[5]);
		if( WifiService::loadCreds(m_savedSsid, sizeof(m_savedSsid), m_savedPass, sizeof(m_savedPass)) ) {
			m_dto.hasSaved = true;
			useSaved();
			copyStr(m_dto.ssid, sizeof(m_dto.ssid), m_savedSsid);
			m_dto.state = WIFI::CONNECTING;
		}
	}
	const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	m_started = WifiService::init(&WifiController::onEvent);
	ESP_LOGI(TAG, "init %s, saved network: %s, internal RAM free %u -> %u B", m_started ? "OK" : "FAILED",
			 m_dto.hasSaved ? m_savedSsid : "(none)", (unsigned)before,
			 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	if( !m_started ) {
		Guard g(m_mutex);
		m_dto.state = WIFI::OFF;
	}
	notifyUi();
}

bool WifiController::startSetup() {
	if( !m_started ) return false;
	Guard g(m_mutex);
	m_setupReq = true;
	m_setupStopReq = false;
	return true;
}

void WifiController::stopSetup() {
	Guard g(m_mutex);
	m_setupReq = false;
	m_setupStopReq = true;
}

bool WifiController::startScan() {
	if( !m_started ) return false;
	{
		Guard g(m_mutex);
		m_scanReq = true;
		m_scanRetryMs = 0;
		m_dto.scanning = true;		// 화면은 바로 "검색 중"
		m_dto.scanDone = false;
	}
	notifyUi();
	return true;
}

bool WifiController::connect(const std::string& ssid, const std::string& pass) {
	if( !m_started || ssid.empty() || ssid.size() > 32 || pass.size() > 64 ) return false;
	{
		Guard g(m_mutex);
		copyStr(m_wantSsid, sizeof(m_wantSsid), ssid.c_str());
		copyStr(m_wantPass, sizeof(m_wantPass), pass.c_str());
		m_trial = true;
		m_tries = 0;
		m_restart = true;
		m_nextTryMs = 0;
		m_dto.state = WIFI::CONNECTING;
		m_dto.fail = WIFI::FAIL_NONE;
		m_dto.failSsid[0] = '\0';
		copyStr(m_dto.ssid, sizeof(m_dto.ssid), m_wantSsid);
		m_dto.ip[0] = '\0';
	}
	ESP_LOGI(TAG, "new network requested: '%s'", ssid.c_str());
	notifyUi();
	return true;
}

void WifiController::forget() {
	Guard g(m_mutex);
	m_forgetReq = true;
}

void WifiController::setDataCallback(DataCallback callback) {
	m_callback = callback;
}

WifiDto WifiController::getState() {
	Guard g(m_mutex);
	return m_dto;
}

void WifiController::scheduleRetry(uint32_t delayMs) {
	m_nextTryMs = millis() + delayMs;
}

void WifiController::useSaved() {
	copyStr(m_wantSsid, sizeof(m_wantSsid), m_savedSsid);
	copyStr(m_wantPass, sizeof(m_wantPass), m_savedPass);
	m_trial = false;
}

void WifiController::notifyUi() {
	m_refreshPending = true;
}

// ---- 이벤트 (이벤트 태스크) ----

void WifiController::onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
	instance().handleEvent(base, id, data);
}

void WifiController::onTimeSync() {
	instance().m_timeSyncPending = true;
}

void WifiController::handleEvent(esp_event_base_t base, int32_t id, void* data) {
	if( base == WIFI_EVENT ) {
		switch( id ) {
			case WIFI_EVENT_STA_CONNECTED: {
				Guard g(m_mutex);
				m_assoc = true;
				break;
			}
			case WIFI_EVENT_STA_DISCONNECTED: {
				const uint8_t reason = ((wifi_event_sta_disconnected_t*)data)->reason;
				Guard g(m_mutex);
				const bool wasConnected = (m_dto.state == WIFI::CONNECTED);
				m_assoc = false;
				m_attempt = false;
				m_dto.ip[0] = '\0';
				if( m_expectDisc ) {
					// 다른 네트워크로 바꾸려고 우리가 끊음 -> 바로 연결
					m_expectDisc = false;
					m_nextTryMs = 0;
					m_dto.state = m_wantSsid[0] ? WIFI::CONNECTING : WIFI::OFF;
				} else if( !m_wantSsid[0] ) {
					m_dto.state = WIFI::OFF;
				} else if( m_trial ) {
					m_tries++;
					ESP_LOGW(TAG, "'%s' try %u failed, reason %u", m_wantSsid, m_tries, reason);
					if( m_tries >= WIFI_CONNECT_TRIES ) {
						// 새 네트워크 실패 확정 -> 결과 표시, 저장된 네트워크로 복귀
						m_dto.state = WIFI::FAILED;
						m_dto.fail = failFromReason(reason);
						copyStr(m_dto.failSsid, sizeof(m_dto.failSsid), m_wantSsid);
						useSaved();
						m_retryIdx = 0;
						scheduleRetry(kRetrySteps[0]);
					} else {
						scheduleRetry(WIFI_TRY_GAP_MS);
					}
				} else {
					if( m_dto.state != WIFI::FAILED ) m_dto.state = WIFI::CONNECTING;
					if( wasConnected ) {
						ESP_LOGW(TAG, "disconnected from '%s', reason %u", m_wantSsid, reason);
						m_retryIdx = 0;
					}
					scheduleRetry(kRetrySteps[m_retryIdx]);
					if( m_retryIdx + 1 < kRetryCount ) m_retryIdx++;
				}
				break;
			}
			case WIFI_EVENT_SCAN_DONE: {
				WIFI::ApInfo aps[WIFI_SCAN_MAX];
				const uint8_t n = WifiService::readScan(aps, WIFI_SCAN_MAX);
				Guard g(m_mutex);
				memcpy(m_dto.aps, aps, sizeof(WIFI::ApInfo) * n);
				m_dto.apCount = n;
				m_dto.scanning = false;
				m_dto.scanDone = true;
				ESP_LOGI(TAG, "scan done: %u networks", n);
				break;
			}
			case WIFI_EVENT_AP_STACONNECTED: {
				Guard g(m_mutex);
				m_dto.apClients++;
				break;
			}
			case WIFI_EVENT_AP_STADISCONNECTED: {
				Guard g(m_mutex);
				if( m_dto.apClients > 0 ) m_dto.apClients--;
				break;
			}
			default:
				return;
		}
		notifyUi();
		return;
	}

	if( base == IP_EVENT && id == IP_EVENT_STA_GOT_IP ) {
		const ip_event_got_ip_t* ev = (const ip_event_got_ip_t*)data;
		bool save = false;
		char ssid[33], pass[65];
		{
			Guard g(m_mutex);
			snprintf(m_dto.ip, sizeof(m_dto.ip), IPSTR, IP2STR(&ev->ip_info.ip));
			m_dto.state = WIFI::CONNECTED;
			m_attempt = false;
			m_retryIdx = 0;
			copyStr(m_dto.ssid, sizeof(m_dto.ssid), m_wantSsid);
			if( m_trial ) {
				copyStr(m_savedSsid, sizeof(m_savedSsid), m_wantSsid);
				copyStr(m_savedPass, sizeof(m_savedPass), m_wantPass);
				m_trial = false;
				m_dto.hasSaved = true;
				save = true;
				copyStr(ssid, sizeof(ssid), m_savedSsid);
				copyStr(pass, sizeof(pass), m_savedPass);
			}
			if( m_dto.setup ) m_setupDoneMs = millis() + WIFI_SETUP_DONE_MS;
			ESP_LOGI(TAG, "connected to '%s', IP %s", m_dto.ssid, m_dto.ip);
		}
		if( save ) WifiService::saveCreds(ssid, pass);
		int8_t rssi;
		if( WifiService::readRssi(rssi) ) {
			Guard g(m_mutex);
			m_dto.rssi = rssi;
		}
		WifiService::startSntp(&WifiController::onTimeSync);
		notifyUi();
	} else if( base == IP_EVENT && id == IP_EVENT_STA_LOST_IP ) {
		{
			Guard g(m_mutex);
			m_dto.ip[0] = '\0';
		}
		notifyUi();
	}
}

// ---- 앱 태스크 ----

void WifiController::uiTick() {
	if( m_started ) {
		const uint32_t now = millis();

		// 저장된 네트워크 삭제
		bool forget;
		{
			Guard g(m_mutex);
			forget = m_forgetReq;
			m_forgetReq = false;
			if( forget ) {
				m_savedSsid[0] = m_savedPass[0] = '\0';
				useSaved();
				m_restart = false;
				m_expectDisc = false;
				m_dto.hasSaved = false;
				m_dto.state = WIFI::OFF;
				m_dto.fail = WIFI::FAIL_NONE;
				m_dto.failSsid[0] = '\0';
				m_dto.ssid[0] = m_dto.ip[0] = '\0';
			}
		}
		if( forget ) {
			WifiService::disconnect();
			WifiService::eraseCreds();
			notifyUi();
		}

		// 설정 모드 시작 / 종료
		bool setupStart = false, setupStop = false;
		char apSsid[33], apPass[33];
		{
			Guard g(m_mutex);
			if( m_setupReq && !m_dto.setup ) {
				setupStart = true;
				copyStr(apSsid, sizeof(apSsid), m_dto.apSsid);
				copyStr(apPass, sizeof(apPass), m_dto.apPass);
			}
			m_setupReq = false;
			if( m_dto.setup && (m_setupStopReq || now - m_setupStartMs > WIFI_SETUP_TIMEOUT_MS ||
								(m_setupDoneMs && (int32_t)(now - m_setupDoneMs) >= 0)) ) {
				setupStop = true;
			}
			m_setupStopReq = false;
		}
		if( setupStart ) {
			const bool ok = WifiService::startAp(apSsid, apPass) && WifiPortal::start();
			{
				Guard g(m_mutex);
				m_dto.setup = ok;
				m_dto.apClients = 0;
				m_setupStartMs = now;
				m_setupDoneMs = 0;
				if( ok ) {
					m_scanReq = true;		// 포털 목록용
					m_dto.scanning = true;
					m_dto.scanDone = false;
				}
			}
			if( !ok ) WifiPortal::stop();
			notifyUi();
		}
		if( setupStop ) {
			WifiPortal::stop();
			WifiService::stopAp();
			{
				Guard g(m_mutex);
				m_dto.setup = false;
				m_dto.apClients = 0;
				m_setupDoneMs = 0;
			}
			notifyUi();
		}

		// 다른 네트워크로 바꾸기: 붙어 있거나 시도 중이면 먼저 끊는다
		bool doDisconnect = false;
		{
			Guard g(m_mutex);
			if( m_restart ) {
				m_restart = false;
				if( m_assoc || m_attempt ) {
					m_expectDisc = true;
					doDisconnect = true;
				}
			}
			// 이벤트 없이 끝난 시도 정리
			if( m_attempt && now - m_attemptMs > kAttemptTimeoutMs ) {
				ESP_LOGW(TAG, "connect attempt timed out");
				m_attempt = false;
				scheduleRetry(WIFI_TRY_GAP_MS);
			}
		}
		if( doDisconnect ) WifiService::disconnect();

		// 검색 (연결 시도 중에는 드라이버가 거부하므로 끝난 뒤에)
		bool doScan = false;
		{
			Guard g(m_mutex);
			if( m_scanReq && !m_attempt && !m_expectDisc && (int32_t)(now - m_scanRetryMs) >= 0 ) doScan = true;
		}
		if( doScan ) {
			const bool ok = WifiService::startScan();
			Guard g(m_mutex);
			if( ok ) m_scanReq = false;
			else m_scanRetryMs = now + 1000;
		}

		// 연결 시도
		bool doConnect = false;
		char ssid[33], pass[65];
		{
			Guard g(m_mutex);
			const bool setupIdle = m_dto.setup && !m_trial;	// 설정 중에는 새 네트워크만 시도 (AP 채널 유지)
			if( m_wantSsid[0] && !m_attempt && !m_assoc && !m_expectDisc && !m_scanReq && !m_dto.scanning &&
				!setupIdle && (int32_t)(now - m_nextTryMs) >= 0 ) {
				doConnect = true;
				m_attempt = true;
				m_attemptMs = now;
				copyStr(ssid, sizeof(ssid), m_wantSsid);
				copyStr(pass, sizeof(pass), m_wantPass);
				if( m_dto.state != WIFI::FAILED ) {
					m_dto.state = WIFI::CONNECTING;
					copyStr(m_dto.ssid, sizeof(m_dto.ssid), m_wantSsid);
				}
			}
		}
		if( doConnect && !WifiService::connect(ssid, pass) ) {
			Guard g(m_mutex);
			m_attempt = false;
			scheduleRetry(WIFI_TRY_GAP_MS);
		}

		// 신호 세기
		bool connected;
		{
			Guard g(m_mutex);
			connected = m_dto.state == WIFI::CONNECTED;
		}
		if( connected && now - m_rssiMs >= WIFI_RSSI_POLL_MS ) {
			m_rssiMs = now;
			int8_t rssi;
			if( WifiService::readRssi(rssi) ) {
				bool changed;
				{
					Guard g(m_mutex);
					changed = WIFI::bars(rssi) != WIFI::bars(m_dto.rssi);
					m_dto.rssi = rssi;
				}
				if( changed ) notifyUi();
			}
		}

		// NTP 시간 -> RTC (센서와 같은 앱 태스크에서 I2C 사용)
		if( m_timeSyncPending.exchange(false) ) {
			time_t t = time(nullptr);
			struct tm lt;
			localtime_r(&t, &lt);
			const bool ok = sensors::setRtcTime(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min,
												lt.tm_sec);
			ESP_LOGI(TAG, "NTP time %04d-%02d-%02d %02d:%02d:%02d -> RTC %s", lt.tm_year + 1900, lt.tm_mon + 1,
					 lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, ok ? "OK" : "FAIL");
			{
				Guard g(m_mutex);
				m_dto.timeSynced = true;
			}
			notifyUi();
		}
	}

	if( m_refreshPending.exchange(false) && m_callback ) {
		m_callback(getState());
	}
}
