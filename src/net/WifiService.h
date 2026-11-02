#ifndef __WIFISERVICE_H__
#define __WIFISERVICE_H__

#include <cstddef>
#include <cstdint>

#include "esp_event.h"
#include "WifiDto.h"

/**
 * @brief ESP-IDF Wi-Fi 호출 모음 (상태 없음 - 상태는 WifiController 가 가진다)
 */
namespace WifiService {
	/**
	 * @brief netif / 이벤트 루프 / Wi-Fi 드라이버 초기화 후 STA 모드로 시작
	 *        handler 는 WIFI_EVENT, IP_EVENT 전부 받는다 (이벤트 태스크에서 호출)
	 */
	bool init(esp_event_handler_t handler);

	/**
	 * @brief 이 기기 STA MAC
	 */
	void readMac(uint8_t mac[6]);

	/**
	 * @brief STA 접속 정보 설정 후 연결 시작 (결과는 이벤트로)
	 */
	bool connect(const char* ssid, const char* pass);

	void disconnect();

	/**
	 * @brief 비동기 검색 시작 (완료: WIFI_EVENT_SCAN_DONE)
	 */
	bool startScan();

	/**
	 * @brief 검색 결과를 신호 센 순서로, 같은 이름은 하나만, 빈 이름 제외
	 */
	uint8_t readScan(WIFI::ApInfo* out, uint8_t max);

	/**
	 * @brief 연결된 AP 신호 세기. 실패 시 false
	 */
	bool readRssi(int8_t& rssi);

	/**
	 * @brief 설정 AP 켜기 (AP+STA) / 끄기 (STA)
	 */
	bool startAp(const char* ssid, const char* pass);
	void stopAp();

	/**
	 * @brief NTP 시작 (한 번만, 이후 SNTP 가 주기적으로 다시 맞춤). 받을 때마다 onSync 호출
	 */
	void startSntp(void (*onSync)());

	/**
	 * @brief 저장된 네트워크 (NVS)
	 */
	bool loadCreds(char* ssid, size_t ssidLen, char* pass, size_t passLen);
	void saveCreds(const char* ssid, const char* pass);
	void eraseCreds();
}

#endif
