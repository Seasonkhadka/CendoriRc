#include "WifiService.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"

namespace {

const char* TAG = "WIFI";

void copyStr(char* dst, size_t dstLen, const char* src) {
	strncpy(dst, src, dstLen - 1);
	dst[dstLen - 1] = '\0';
}

} // namespace

namespace WifiService {

bool init(esp_event_handler_t handler) {
	ESP_ERROR_CHECK(esp_netif_init());
	esp_err_t err = esp_event_loop_create_default();
	if( err != ESP_OK && err != ESP_ERR_INVALID_STATE ) {
		ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
		return false;
	}
	esp_netif_create_default_wifi_sta();
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&cfg);
	if( err != ESP_OK ) {
		ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
		return false;
	}
	// 접속 정보는 우리가 NVS "wifi" 에 직접 저장 - 드라이버의 플래시 저장은 끈다
	esp_wifi_set_storage(WIFI_STORAGE_RAM);
	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handler, nullptr);
	esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handler, nullptr);

	esp_wifi_set_mode(WIFI_MODE_STA);
	err = esp_wifi_start();
	if( err != ESP_OK ) {
		ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

void readMac(uint8_t mac[6]) {
	if( esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK ) memset(mac, 0, 6);
}

bool connect(const char* ssid, const char* pass) {
	wifi_config_t c = {};
	memcpy(c.sta.ssid, ssid, std::min(strlen(ssid), sizeof(c.sta.ssid)));
	memcpy(c.sta.password, pass, std::min(strlen(pass), sizeof(c.sta.password)));
	c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
	c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
	c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
	esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &c);
	if( err == ESP_OK ) err = esp_wifi_connect();
	if( err != ESP_OK ) {
		ESP_LOGW(TAG, "connect '%s': %s", ssid, esp_err_to_name(err));
		return false;
	}
	ESP_LOGI(TAG, "connecting to '%s'", ssid);
	return true;
}

void disconnect() {
	esp_wifi_disconnect();
}

bool startScan() {
	wifi_scan_config_t sc = {};
	sc.show_hidden = false;
	esp_err_t err = esp_wifi_scan_start(&sc, false);
	if( err != ESP_OK ) {
		ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

uint8_t readScan(WIFI::ApInfo* out, uint8_t max) {
	uint16_t n = 0;
	esp_wifi_scan_get_ap_num(&n);
	if( n == 0 ) {
		esp_wifi_clear_ap_list();
		return 0;
	}
	wifi_ap_record_t* recs = (wifi_ap_record_t*)calloc(n, sizeof(wifi_ap_record_t));
	if( !recs ) {
		esp_wifi_clear_ap_list();
		return 0;
	}
	esp_wifi_scan_get_ap_records(&n, recs);	// 결과 목록 해제까지 포함
	std::sort(recs, recs + n, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });

	uint8_t count = 0;
	for( uint16_t i = 0; i < n && count < max; i++ ) {
		const char* name = (const char*)recs[i].ssid;
		if( !name[0] ) continue;
		bool dup = false;
		for( uint8_t j = 0; j < count; j++ ) {
			if( strcmp(out[j].ssid, name) == 0 ) dup = true;
		}
		if( dup ) continue;
		copyStr(out[count].ssid, sizeof(out[count].ssid), name);
		out[count].rssi = recs[i].rssi;
		out[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
		count++;
	}
	free(recs);
	return count;
}

bool readRssi(int8_t& rssi) {
	wifi_ap_record_t ap;
	if( esp_wifi_sta_get_ap_info(&ap) != ESP_OK ) return false;
	rssi = ap.rssi;
	return true;
}

bool startAp(const char* ssid, const char* pass) {
	wifi_config_t a = {};
	copyStr((char*)a.ap.ssid, sizeof(a.ap.ssid), ssid);
	a.ap.ssid_len = strlen(ssid);
	copyStr((char*)a.ap.password, sizeof(a.ap.password), pass);
	a.ap.channel = WIFI_AP_CHANNEL;
	a.ap.max_connection = WIFI_AP_MAX_CONN;
	a.ap.authmode = WIFI_AUTH_WPA2_PSK;
	esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
	if( err == ESP_OK ) err = esp_wifi_set_config(WIFI_IF_AP, &a);
	if( err != ESP_OK ) {
		ESP_LOGE(TAG, "start AP: %s", esp_err_to_name(err));
		esp_wifi_set_mode(WIFI_MODE_STA);
		return false;
	}
	ESP_LOGI(TAG, "setup AP '%s' started", ssid);
	return true;
}

void stopAp() {
	esp_wifi_set_mode(WIFI_MODE_STA);
	ESP_LOGI(TAG, "setup AP stopped");
}

void startSntp(void (*onSync)()) {
	static bool started = false;
	if( started ) return;
	started = true;
	setenv("TZ", WIFI_TZ, 1);
	tzset();
	static void (*s_onSync)() = nullptr;
	s_onSync = onSync;
	esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(WIFI_NTP_SERVER);
	cfg.sync_cb = [](struct timeval*) {
		if( s_onSync ) s_onSync();
	};
	esp_netif_sntp_init(&cfg);
	ESP_LOGI(TAG, "SNTP started (%s)", WIFI_NTP_SERVER);
}

bool loadCreds(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
	nvs_handle_t h;
	if( nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK ) return false;
	size_t sl = ssidLen, pl = passLen;
	const bool ok = nvs_get_str(h, "ssid", ssid, &sl) == ESP_OK && nvs_get_str(h, "pass", pass, &pl) == ESP_OK &&
					ssid[0] != '\0';
	nvs_close(h);
	return ok;
}

void saveCreds(const char* ssid, const char* pass) {
	nvs_handle_t h;
	if( nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK ) return;
	nvs_set_str(h, "ssid", ssid);
	nvs_set_str(h, "pass", pass);
	nvs_commit(h);
	nvs_close(h);
	ESP_LOGI(TAG, "saved network '%s'", ssid);
}

void eraseCreds() {
	nvs_handle_t h;
	if( nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK ) return;
	nvs_erase_all(h);
	nvs_commit(h);
	nvs_close(h);
	ESP_LOGI(TAG, "saved network erased");
}

} // namespace WifiService
