#ifndef __RS485SERVICE_H__
#define __RS485SERVICE_H__

#include <cstdint>

#include "rc/RcDto.h"
#include "Rs485Struct.h"

/**
 * @brief RS-485 외부 프로토콜 프레임 변환 (상태 없음, 호스트 테스트 가능)
 */
namespace Rs485Service {
	/**
	 * @brief HEADER ~ D5 XOR
	 */
	uint8_t makeChecksum(const RS485::Frame& f);
	bool checkChecksum(const RS485::Frame& f);

	/**
	 * @brief PC 가 보내는 요청 헤더인지 (프레임 시작 동기화용)
	 */
	bool isRequestHeader(uint8_t header);

	/**
	 * @brief 요청에 대한 응답 프레임 (현재 상태 + 센서). 응답 없는 헤더면 false
	 */
	bool buildReply(const RS485::Frame& req, uint8_t group, uint8_t id, const RcDto& dto,
					const RC::SensorCache& sensor, RS485::Frame& out);

	/**
	 * @brief VSP: RC 값 (0.05V 단위) <-> 프로토콜 값 (RS485_VSP_X10)
	 */
	uint8_t encodeVsp(uint8_t units005);
	int decodeVspX10(uint8_t value);		// 전압 x10 (2.3V -> 23)

	/**
	 * @brief 예약시간 분 -> 프로토콜 시간 (올림, 최대 24)
	 */
	uint8_t reserveToWire(uint16_t minutes);

	/**
	 * @brief 디버그 출력용 "D5 00 01 ..."
	 */
	void toHex(const RS485::Frame& f, char* buf, int len);
}

#endif
