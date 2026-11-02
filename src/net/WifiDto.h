#ifndef __WIFIDTO_H__
#define __WIFIDTO_H__

#include <cstdint>

#include "WifiConfig.h"

namespace WIFI {
	/**
	 * @brief 스테이션(공유기 연결) 상태
	 */
	enum State : uint8_t {
		OFF = 0,			// 저장된 네트워크 없음 / 연결 안 함
		CONNECTING,			// 연결 시도 중 (재연결 포함)
		CONNECTED,			// IP 받음
		FAILED				// 새 네트워크 연결 실패 확정 (fail 참고)
	};

	/**
	 * @brief 연결 실패 원인
	 */
	enum Fail : uint8_t {
		FAIL_NONE = 0,
		FAIL_AUTH,			// 비밀번호 오류
		FAIL_NO_AP,			// 네트워크 없음
		FAIL_OTHER
	};

	/**
	 * @brief 검색된 네트워크 1개
	 */
	struct ApInfo {
		char ssid[33];		// 이름 (null 종료)
		int8_t rssi;		// 신호 세기 dBm
		bool secure;		// 비밀번호 필요
	};

	/**
	 * @brief 신호 세기 -> 막대 수 (0~4)
	 */
	inline int bars(int8_t rssi) {
		if( rssi >= -55 ) return 4;
		if( rssi >= -65 ) return 3;
		if( rssi >= -75 ) return 2;
		if( rssi >= -85 ) return 1;
		return 0;
	}
}

/**
 * @brief Wi-Fi 상태 복사본 (UI 는 이것만 읽는다)
 */
struct WifiDto {
	WIFI::State state = WIFI::OFF;
	WIFI::Fail fail = WIFI::FAIL_NONE;	// 마지막 새 네트워크 실패 원인 (다음 연결 요청까지 유지)
	char failSsid[33] = "";			// 실패한 새 네트워크 이름
	bool hasSaved = false;			// 저장된 네트워크 있음
	char ssid[33] = "";				// 연결(시도) 중인 네트워크
	char ip[16] = "";				// 연결 시 IP
	int8_t rssi = 0;				// 연결 시 신호 세기 dBm
	char mac[18] = "";				// 이 기기 MAC (AA:BB:CC:DD:EE:FF)

	bool setup = false;				// 설정 모드 (AP + 포털) 동작 중
	char apSsid[33] = "";			// 설정 AP 이름
	char apPass[33] = "";			// 설정 AP 비밀번호
	uint8_t apClients = 0;			// 설정 AP 에 붙은 휴대폰 수

	bool scanning = false;			// 검색 중
	bool scanDone = false;			// 검색 결과 있음 (0개 포함)
	uint8_t apCount = 0;
	WIFI::ApInfo aps[WIFI_SCAN_MAX];	// 신호 센 순서

	bool timeSynced = false;		// NTP 시간 받음

	bool connected() const { return state == WIFI::CONNECTED; }
};

#endif
