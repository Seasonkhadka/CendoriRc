#ifndef __WIFICONFIG_H__
#define __WIFICONFIG_H__

/**
 * @brief Wi-Fi 연결 설정값 - 바꿀 수 있는 값은 모두 여기에 둔다
 */

// ---- 설정용 AP (스마트폰 QR 연결) ----
#define WIFI_AP_SSID_PREFIX		"CENDORI-RC-"	// + MAC 마지막 2바이트 (대문자 hex 4자리)
#define WIFI_AP_PASS_PREFIX		"cendori"		// + MAC 마지막 2바이트 (소문자 hex 4자리) = 11자
#define WIFI_AP_CHANNEL			1
#define WIFI_AP_MAX_CONN		2
#define WIFI_AP_IP				"192.168.4.1"	// ESP-IDF 기본 AP 주소 (포털 리다이렉트용)
#define WIFI_SETUP_TIMEOUT_MS	(10UL * 60UL * 1000UL)	// 설정 모드 자동 종료
#define WIFI_SETUP_DONE_MS		5000			// 연결 성공 후 휴대폰에 결과 페이지를 보여줄 시간

// ---- 연결 / 재연결 ----
#define WIFI_CONNECT_TRIES		3				// 새 네트워크: 이 횟수 실패하면 연결 실패 확정
#define WIFI_TRY_GAP_MS			1000			// 새 네트워크 재시도 간격
#define WIFI_RETRY_STEPS_MS		{ 1000, 5000, 10000, 30000 }	// 저장된 네트워크 재연결 간격 (마지막 값 반복)
#define WIFI_RSSI_POLL_MS		5000			// 연결 중 신호 세기 갱신 주기

// ---- 검색 ----
#define WIFI_SCAN_MAX			12				// 화면/포털에 보여줄 최대 네트워크 수

// ---- 시간 (NTP) ----
#define WIFI_NTP_SERVER			"pool.ntp.org"
#define WIFI_TZ					"KST-9"			// 한국 표준시 (UTC+9)

// ---- 저장 ----
#define WIFI_NVS_NAMESPACE		"wifi"

#endif
