#ifndef __RCCONTROLLER_H__
#define __RCCONTROLLER_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "RcDto.h"

/**
 * @brief 룸콘 상태 단일 소유자
 *
 * - UI -> 상태: set*() 이 RcDto 를 바꾸고 다음 응답이 MAIN 에 전달 (직접 송신 없음)
 * - MAIN -> 상태: handleFrame() 이 RcDto 를 갱신하고 UI 태스크에 갱신 요청
 * - 상태 -> MAIN: buildReply() 가 상태 복사본으로 응답 프레임 생성
 * mutex 는 필드 복사/변경 동안만 잡는다 (그리기, I/O 중에는 잡지 않음).
 */
class RcController {
public:
	typedef std::function<void(const RcDto&)> DataCallback;

	static RcController& instance();

	/**
	 * @brief NVS 에서 사용자 설정 로드 - 첫 응답 전에 호출
	 */
	void init();

	/**
	 * @brief 전원 On: 가동 (풍량 0 이면 1) / Off: 정지 (풍량 유지)
	 */
	bool setPowerState(bool powerOn);

	/**
	 * @brief "ventilation", "bypass", "clean", "auto", "ai" - 모드 비트만 변경
	 *        전원 Off 상태에서는 거부 (구 RC 2.4.1)
	 */
	bool setModeState(const std::string& mode);

	/**
	 * @brief "1" ~ "3" - 풍량만 변경 (정지는 전원 Off)
	 *        자동 / 취침 중에는 거부 (구 RC 2.6.1, 2.3.6)
	 */
	bool setVolumeState(const std::string& volume);

	/**
	 * @brief B5 Data1 ID (1~99, 값 그대로 98 -> 0x62)
	 */
	bool setDeviceId(int id);

	/**
	 * @brief 구 RC 예약 순서: "start_reserve_X", "stop_reserve_X" (X: 분, 10분 단위, 10~1440),
	 *        "sleep" (취침, 항상 약풍), "cancel" (예약/취침 취소)
	 */
	bool setReservationState(const std::string& reservation);

	/**
	 * @brief 전열 VSP 전압 설정 (V, 0.05V 단위 "2.45"). 0.5V < 값 < 5.5V
	 */
	bool setVspSetting(const std::string& exhstH, const std::string& exhstM, const std::string& exhstL,
				const std::string& airSplH, const std::string& airSplM, const std::string& airSplL);

	/**
	 * @brief 바이패스 VSP 전압 설정 (형식 동일, C5 mode 3/4 로 전송)
	 */
	bool setVspBypassSetting(const std::string& exhstH, const std::string& exhstM, const std::string& exhstL,
				const std::string& airSplH, const std::string& airSplM, const std::string& airSplL);

	/**
	 * @brief 필터 초기화 - 필터시간을 초기값으로 (B5 Data2 = 5)
	 */
	bool setFilterReset();

	/**
	 * @brief 필터 초기값 설정 (x100h, 1~99) 후 MAIN 에 전송
	 */
	bool setFilterInitial(int x100);

	/**
	 * @brief "exhaust", "supply", "sleep", "heater", "lock", "clean", "dust"
	 */
	bool setFunctionState(const std::string& func, bool on);

	/**
	 * @brief UI 갱신용 - UI 태스크에서만 호출됨
	 */
	void setDataCallback(DataCallback callback);

	/**
	 * @brief 룸콘 자체 센서 최신값 (B6/B7/B8 응답용)
	 */
	void setLocalSensors(const RC::SensorCache& sensor);

	/**
	 * @brief 현재 상태 복사본
	 */
	RcDto getState();

	/**
	 * @brief 룸콘 자체 센서 최신값 복사본 (RS-485 응답용)
	 */
	RC::SensorCache getSensors();

	/**
	 * @brief UI 태스크 루프에서 호출: 갱신 요청 처리, 링크 상태, NVS 지연 저장
	 */
	void uiTick();

	/**
	 * @brief PLC 수신 프레임 처리 (체크섬 확인 완료)
	 */
	void handleFrame(const uint8_t frame[6]);

	/**
	 * @brief header 에 대한 응답 생성. 응답 없으면 false
	 */
	bool buildReply(uint8_t header, uint8_t out[6]);

private:
	enum ReserveState { RESV_NONE = 0, RESV_ARMED, RESV_READY };
	enum VspPending {
		VSP_PEND_EXHAUST = 0x01, VSP_PEND_SUPPLY = 0x02,
		VSP_PEND_BP_EXHAUST = 0x04, VSP_PEND_BP_SUPPLY = 0x08
	};

	RcController();

	// 아래 함수들은 mutex 잡은 상태에서 호출
	bool lockedOut(bool unlocking) const;
	void markLocalChange();
	bool setVsp(bool bypass, const std::string* volts);
	bool evaluateAuto();		// 자동 모드 결과 갱신, 바뀌면 true

	void countFilterTime();				// UI 태스크: RC 자체 필터 사용시간 (분)
	void saveFilterMinutes(uint32_t minutes);
	uint32_t loadFilterMinutes();

	void notifyUi();		// UI 갱신 요청만 등록 (어느 태스크에서든 호출 가능)
	void dispatchUi();		// UI 태스크: 상태 복사 후 콜백
	void saveSettings(const RcDto& dto);
	bool loadSettings(RcDto& dto);

	SemaphoreHandle_t m_mutex;
	RcDto m_dto;
	RC::SensorCache m_sensor;
	DataCallback m_callback;

	bool m_ctrlPending;		// 다음 B5 에 제어 FLAG
	bool m_funcPending;		// B5 Data2 = 8 대기
	bool m_filterPending;		// B5 Data2 = 5 대기
	bool m_autoValid;			// 자동 결과 계산됨 (자동 진입 시 false -> 즉시 계산)
	uint32_t m_autoChangedMs;	// 마지막 자동 결과 변경 시각
	uint8_t m_resvState;		// ReserveState
	uint8_t m_resvSel;			// B5_RUN_RSV_TM / B5_STOP_RSV_TM
	uint8_t m_vspPending;		// VspPending 비트
	bool m_nvsDirty;
	uint32_t m_lastChangeMs;

	std::atomic<bool> m_refreshPending;
	bool m_linkLost;

	uint32_t m_filterTickMs;	// countFilterTime 마지막 호출 시각
	uint32_t m_filterRunMs;		// 아직 1분이 안 된 사용시간 (ms)
};

// ---- PLC 계층 연결 (plc_protocol 에서 호출) ----

// 유효한 프레임 수신 시 호출 (복호화 + 체크섬 확인 완료, 6바이트)
void onPlcFrame(const uint8_t frame[6]);

// 응답 시점(MAIN 프레임 후 140 ms)에 호출 - 보낼 6바이트를 채워서 true 반환
bool onPlcReplyDue(uint8_t header, uint8_t out[6]);

#endif
