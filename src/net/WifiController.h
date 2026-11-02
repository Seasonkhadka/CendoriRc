#ifndef __WIFICONTROLLER_H__
#define __WIFICONTROLLER_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "WifiDto.h"

/**
 * @brief Wi-Fi 상태 단일 소유자
 *
 * - UI / 포털 -> 명령: startSetup(), startScan(), connect(), forget() 는 요청만 기록
 * - 실제 Wi-Fi 호출, 재연결 시각, 설정 모드 종료, NTP -> RTC 는 uiTick() (앱 태스크) 에서 처리
 * - 드라이버 이벤트는 이벤트 태스크에서 onEvent() 로 들어와 상태만 바꾼다
 * mutex 는 필드 복사/변경 동안만 잡는다. 화면/PLC 는 Wi-Fi 때문에 멈추지 않는다.
 */
class WifiController {
public:
	typedef std::function<void(const WifiDto&)> DataCallback;

	static WifiController& instance();

	/**
	 * @brief 드라이버 시작, 저장된 네트워크가 있으면 연결 시작 (NVS 초기화 후 호출)
	 */
	void init();

	/**
	 * @brief 설정 모드 시작: 설정 AP + 웹 포털 (QR 연결)
	 */
	bool startSetup();

	/**
	 * @brief 설정 모드 종료
	 */
	void stopSetup();

	/**
	 * @brief 주변 네트워크 검색 요청 (결과는 상태의 aps)
	 */
	bool startScan();

	/**
	 * @brief 새 네트워크 연결 요청. 성공하면 저장, 실패하면 이전 네트워크로 돌아감
	 */
	bool connect(const std::string& ssid, const std::string& pass);

	/**
	 * @brief 연결 끊고 저장된 네트워크 삭제
	 */
	void forget();

	/**
	 * @brief UI 갱신용 - UI 태스크에서만 호출됨
	 */
	void setDataCallback(DataCallback callback);

	/**
	 * @brief 현재 상태 복사본
	 */
	WifiDto getState();

	/**
	 * @brief 앱(UI) 태스크 루프에서 호출
	 */
	void uiTick();

private:
	WifiController();

	static void onEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
	static void onTimeSync();
	void handleEvent(esp_event_base_t base, int32_t id, void* data);

	// mutex 잡은 상태에서 호출
	void scheduleRetry(uint32_t delayMs);
	void useSaved();

	void notifyUi();

	SemaphoreHandle_t m_mutex;
	WifiDto m_dto;
	DataCallback m_callback;
	bool m_started;

	char m_savedSsid[33];		// NVS 에 저장된 네트워크
	char m_savedPass[65];
	char m_wantSsid[33];		// 지금 연결하려는 네트워크 (저장된 것 또는 새 것)
	char m_wantPass[65];
	bool m_trial;				// m_want 가 아직 저장 안 된 새 네트워크
	uint8_t m_tries;			// 새 네트워크 실패 횟수
	uint8_t m_retryIdx;			// 저장된 네트워크 재연결 간격 단계

	bool m_attempt;				// esp_wifi_connect 후 결과 대기
	uint32_t m_attemptMs;		// 시도 시작 시각 (응답 없는 시도 정리용)
	bool m_assoc;				// AP 에 붙어 있음
	bool m_restart;				// 붙어 있거나 시도 중일 때 다른 네트워크로 바꾸기
	bool m_expectDisc;			// 우리가 끊은 것 - 실패로 세지 않고 바로 다시 연결
	uint32_t m_nextTryMs;

	bool m_scanReq;				// 검색 요청 대기 (연결 시도 끝나면 시작)
	uint32_t m_scanRetryMs;		// 검색 시작 실패 시 다음 시도 시각
	bool m_setupReq;			// 설정 모드 시작 요청
	bool m_setupStopReq;		// 설정 모드 종료 요청
	bool m_forgetReq;
	uint32_t m_setupStartMs;
	uint32_t m_setupDoneMs;		// 연결 성공 후 설정 모드 종료 예정 시각 (0 = 없음)
	uint32_t m_rssiMs;

	std::atomic<bool> m_timeSyncPending;
	std::atomic<bool> m_refreshPending;
};

#endif
