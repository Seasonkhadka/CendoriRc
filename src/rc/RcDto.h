#ifndef __RCDTO_H__
#define __RCDTO_H__

#include <cstdint>

#include "io/PLCStruct.h"

namespace RC {

	// RcDto.mode == B5 Data2 (0~3)
	enum Mode {
		STOP = 0, RUN = 1, START_RESERVE = 2, STOP_RESERVE = 3
	};

	const uint8_t VOLUME_MAX = 3;		// 0:정지 1:약 2:중 3:강
	const uint8_t VSP_MIN = 10;		// 0.05V 단위 (= PLC 값, V x 20), 초과해야 유효 (0.5V)
	const uint8_t VSP_MAX = 110;		// 0.05V 단위, 미만이어야 유효 (5.5V)

	// 모드 열 (하나만 선택, 풍량/전원/잠금은 그대로)
	enum UiMode {
		UI_VENTILATION = 0, UI_BYPASS, UI_CLEAN, UI_AUTO, UI_AI
	};
	const uint8_t UI_MODE_COUNT = 5;

	// rxMask: 한 번이라도 수신한 프레임
	enum RxMask {
		RX_D5 = 0x01, RX_E5 = 0x02, RX_D6 = 0x04, RX_D7 = 0x08,
		RX_E7 = 0x10, RX_D8 = 0x20, RX_D9 = 0x40, RX_A6 = 0x80
	};

	/**
	 * @brief 룸콘 자체 센서 최신값 (B6/B7/B8 응답용)
	 */
	typedef struct {
		bool hasTemp = false;
		float tempC = 0;		// 실내온도
		float humidity = 0;	// 습도 %
		bool hasCo2 = false;
		int co2 = 0;			// ppm
		bool hasVoc = false;
		int tvoc = 0;			// ug/m3 (SGP40 VOC Index 환산)
		bool hasPm = false;
		int pm25 = 0;			// ug/m3
		int pm10 = 0;			// ug/m3
		int pm1 = 0;			// ug/m3 (RS-485 BB)
		bool hasCo = false;
		int co = 0;			// ppm
	} SensorCache;
}

/**
 * @brief 룸콘 상태 - 사용자 설정 + MAIN 수신값
 *
 * RcController 가 단일 소유자. 사용자 설정은 B5/B7/C5 응답으로 MAIN 에 전달되고,
 * 수신값은 표시 전용 (E5/E7 만 설정을 변경).
 */
class RcDto {
public:
	// ---- 사용자 설정 (NVS 저장) ----
	uint8_t mode;			// RC::Mode
	uint8_t volume;		// 0~3 (0 = 정지, 1로 바꾸지 않음)
	bool bypass;			// 0:전열 1:바이패스
	bool heater;			// 히터 사용
	bool clean;			// 청정모드
	bool autoDrive;		// 자동모드
	bool ahuLock;			// 잠금
	bool exhaust;			// 배기모드
	bool supply;			// 급기모드
	bool night;			// 취침모드
	bool dust;				// 먼지제거
	bool aiMode;			// AI 모드 (RC 전용, 프로토콜에 없음)
	uint8_t deviceId;		// B5 Data1 (사용자 선택 1~99, 기본 DEFAULT_DEVICE_ID)
	uint16_t startMin;		// 가동예약 시간 (분, 10분 단위, MAX 1440)
	uint16_t stopMin;		// 정지예약 시간 (분, 10분 단위, MAX 1440)
	uint8_t filterInit;	// 필터 초기값 (x100h, 20 = 2,000h)
	uint8_t vspExhaustL;	// 배기 전열 VSP 약풍 (0.05V 단위 = V x 20)
	uint8_t vspExhaustM;	// 배기 전열 VSP 중풍
	uint8_t vspExhaustH;	// 배기 전열 VSP 강풍
	uint8_t vspSupplyL;	// 급기 전열 VSP 약풍
	uint8_t vspSupplyM;	// 급기 전열 VSP 중풍
	uint8_t vspSupplyH;	// 급기 전열 VSP 강풍
	uint8_t vspBpExhaustL;	// 배기 바이패스 VSP 약풍
	uint8_t vspBpExhaustM;
	uint8_t vspBpExhaustH;
	uint8_t vspBpSupplyL;	// 급기 바이패스 VSP 약풍
	uint8_t vspBpSupplyM;
	uint8_t vspBpSupplyH;

	// ---- 자동 모드 결과 (RC 계산, 저장 안 함) ----
	uint8_t autoVolume;	// Table 4 풍량 (0: 정지)
	bool autoClean;		// Table 4 청정 여부

	// ---- MAIN 수신값 (표시 전용) ----
	uint8_t errCode;		// D5 Data4 (Err_D5)
	int16_t outdoorTemp;	// 외기온도 ℃
	uint16_t mainCo2;		// MAIN CO2 ppm
	bool heaterRunning;	// 히터 가동중 (D6 / E5 Function)
	uint8_t filterRemain;	// 필터 잔여시간 (100h 단위)
	uint16_t filterUsage;	// 필터 사용% (RC_FILTER_USAGE)

	// ---- RC 자체 필터 사용시간 (RC_FILTER_LOCAL_COUNT, NVS 별도 저장) ----
	uint32_t filterUsedMin;	// 마지막 필터 초기화 후 사용시간 (분)
	bool fireAlarm;		// 화재알람
	bool heat;				// E5 HEAT
	bool bathLink;			// 욕실연동
	bool hoodLink;			// 주방연동

	// ---- 외부 센서 (D8 / D9) ----
	int16_t extTemp;		// D8 온도 ℃
	uint8_t extHumidity;	// D8 습도 %
	uint8_t extPm25;		// D8 PM2.5
	uint8_t extPm10;		// D8 PM10
	uint16_t extTvoc;		// D9 TVOC
	int16_t extTemp2;		// D9 온도 ℃
	uint8_t extHumidity2;	// D9 습도 %

	// ---- 링크 ----
	uint8_t rxMask;		// RC::RxMask
	bool linkLost;			// RC_LINK_TIMEOUT_MS 초과

	RcDto();
	RcDto(const RcDto& other);
	RcDto& operator=(const RcDto& other);

	/**
	 * @brief 첫 부팅 기본값: 가동, 풍량 1, 전열, 나머지 해제
	 */
	void setDefaults();

	/**
	 * @brief 사용자 설정 필드만 비교 (E5/E7 변경 감지, NVS 저장 판단)
	 */
	bool sameSettings(const RcDto& other) const;

	/**
	 * @brief 모드 열 선택 - 모드 비트만 변경 (풍량/전원/잠금/히터 유지)
	 */
	bool applyUiMode(RC::UiMode m);

	/**
	 * @brief 현재 모드 비트 -> 모드 열 값
	 */
	RC::UiMode uiMode() const;

	/**
	 * @brief 풍량만 변경, 모드 유지
	 */
	void setVolume(uint8_t v);

	/**
	 * @brief B5 Data1 ID 변경 (값 그대로, 98 -> 0x62)
	 */
	void setDeviceId(uint8_t id);

	/**
	 * @brief 전원 On 상태 (가동 또는 정지예약 대기)
	 */
	bool isRunning() const;

	/**
	 * @brief RC 자동 모드 동작 중 (자동, AI 아님)
	 */
	bool autoActive() const;

	/**
	 * @brief 표시할 필터 값이 있음 (MAIN D6 수신 또는 RC 자체 계산)
	 */
	bool hasFilterInfo() const;

	/**
	 * @brief 필터 잔여시간 (h): MAIN D6 우선, 없으면 초기값 - RC 사용시간
	 */
	int filterRemainHours() const;

	/**
	 * @brief 필터 사용시간 (h)
	 */
	int filterUsedHours() const;

	/**
	 * @brief 필터 잔여율 (0~100 %)
	 */
	int filterRemainPct() const;

	/**
	 * @brief 필터 교체 알람: MAIN 알람 비트 또는 RC 사용시간 >= 초기값
	 */
	bool filterReplaceDue() const;

	/**
	 * @brief 실제 송신 풍량: 취침 -> 약풍, 자동 -> autoVolume, 그 외 volume
	 */
	uint8_t effectiveVolume() const;

	/**
	 * @brief 실제 송신 청정 비트: 자동 -> autoClean, 그 외 clean
	 */
	bool effectiveClean() const;

	/**
	 * @brief D5 / E5 반영. E5 는 FE 아닌 필드만 적용
	 * @return 사용자 설정이 바뀌었으면 true
	 */
	bool structConverter(const PLC::d5e5b5& packet);

	/**
	 * @brief D6 반영 (히터 가동, 필터)
	 */
	bool structConverter(const PLC::d6b6& packet);

	/**
	 * @brief E7 반영 (D7 은 상태요청이라 값 없음)
	 * @return 사용자 설정이 바뀌었으면 true
	 */
	bool structConverter(const PLC::d7e7b7& packet);

	/**
	 * @brief D8 외부 센서 반영
	 */
	bool structConverter(const PLC::d8& packet);

	/**
	 * @brief D9 외부 센서 반영
	 */
	bool structConverter(const PLC::d9& packet);

	/**
	 * @brief A6 VSP 반영 (x20 값 그대로 저장)
	 * @return VSP 설정이 바뀌었으면 true
	 */
	bool structConverter(const PLC::a6& packet);

	/**
	 * @brief B5 Data3: 바이패스, 히터, 제어FLAG, 청정, 자동, 잠금, 풍량
	 */
	uint8_t makeB5Func(bool ctrlFlag) const;

	/**
	 * @brief B5 Data4 (Data2 = 8): 배기, 급기, 먼지제거, 취침
	 */
	uint8_t makeB5Func8() const;

	/**
	 * @brief 현재 상태로 B5 (상태요청1 / 제어요청 응답) 생성
	 */
	PLC::d5e5b5 makeB5(bool ctrlFlag) const;

private:
	/**
	 * @brief MAIN 이 모드 비트를 바꿔 AI 와 맞지 않으면 AI 해제
	 */
	void dropAiIfOverridden();
};

#endif
