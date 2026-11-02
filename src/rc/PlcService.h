#ifndef __PLCSERVICE_H__
#define __PLCSERVICE_H__

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_log.h"
#include "io/PLCStruct.h"
#include "RcDto.h"

#ifndef LOGI
#define LOGI(fmt, ...) ESP_LOGI("PLC", fmt, ##__VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(fmt, ...) ESP_LOGE("PLC", fmt, ##__VA_ARGS__)
#endif
#ifndef LOGW
#define LOGW(fmt, ...) ESP_LOGW("PLC", fmt, ##__VA_ARGS__)
#endif

/**
 * @brief RC -> MAIN 프레임 생성 (B5 / B6 / B7 / B8 / C5)
 *
 * 모두 6바이트, 체크섬까지 채워서 반환. 라인 인코딩은 plc_link 가 적용.
 */
class PlcService {
public:
	/**
	 * @brief Header ~ Data4 합의 2의 보수 (6바이트 합 = 0x00)
	 */
	static uint8_t makeChecksum(const uint8_t* buf);

	/**
	 * @brief 수신 프레임 체크섬 확인
	 */
	static bool checkChecksum(const uint8_t* buf);

	/**
	 * @brief 예약 분 -> B5 Data4 값 (RC_RESERVE_TX_UNIT_MIN 단위, 올림)
	 */
	static uint8_t encodeReserve(uint16_t minutes);

	/**
	 * @brief B5 예약 시간 프레임 (Data2 = 6 가동예약 / 7 정지예약, Data4 = encodeReserve(분))
	 */
	static PLC::d5e5b5 makeB5Time(const RcDto& dto, uint8_t timeSel, uint16_t minutes, bool ctrlFlag);

	/**
	 * @brief B5 필터 프레임 (Data2 = 5, Data4 = 필터 초기값 x100h)
	 */
	static PLC::d5e5b5 makeB5Filter(const RcDto& dto, bool ctrlFlag);

	/**
	 * @brief B5 Function 프레임 (Data2 = 8, Data4 = 배기/급기/먼지제거/취침)
	 */
	static PLC::d5e5b5 makeB5Function(const RcDto& dto, bool ctrlFlag);

	/**
	 * @brief C5 VSP 변경 (팬 1개, 0.05V 단위 = PLC 값 그대로)
	 * @param vspMode PLC::Mode_C5_Data1
	 */
	static PLC::c5 makeC5(uint8_t vspMode, uint8_t low, uint8_t mid, uint8_t high);

	/**
	 * @brief B6: CO2 / 20, PM2.5, TVOC 상위/하위
	 */
	static PLC::d6b6 makeB6(const RC::SensorCache& sensor);

	/**
	 * @brief B7: 실내온도 + 20, PM10, 필터알람/배기/급기/취침, 습도
	 */
	static PLC::d7e7b7 makeB7(const RcDto& dto, const RC::SensorCache& sensor);

	/**
	 * @brief B8: CO 상위/하위
	 */
	static PLC::b8 makeB8(const RC::SensorCache& sensor);

	/**
	 * @brief 로그용 "B5 00 01 01 00 49"
	 */
	static std::string toHex(const uint8_t* buf, size_t len);

	/**
	 * @brief 프레임 구조체 -> 6바이트
	 */
	template <typename T>
	static void toBytes(const T& packet, uint8_t out[PLC::FRAME_LEN]) {
		const uint8_t* p = (const uint8_t*)(&packet);
		for( int i = 0 ; i < PLC::FRAME_LEN ; i++ ) {
			out[i] = p[i];
		}
	}

	/**
	 * @brief 6바이트 -> 프레임 구조체
	 */
	template <typename T>
	static T fromBytes(const uint8_t in[PLC::FRAME_LEN]) {
		T packet;
		uint8_t* p = (uint8_t*)(&packet);
		for( int i = 0 ; i < PLC::FRAME_LEN ; i++ ) {
			p[i] = in[i];
		}
		return packet;
	}
};

#endif
