#ifndef __RS485CONTROLLER_H__
#define __RS485CONTROLLER_H__

#include <cstdint>

#include "Rs485Struct.h"

/**
 * @brief RS-485 외부 프로토콜 - 이 기기가 MAIN 역할 (PC 가 Polling, 우리는 응답만)
 *
 * - 주소: Group = RS485_GROUP, ID = RC 설정의 ID (기기 설정 -> ID 설정)
 * - 제어요청 (E5 / E7 / C5) 은 RcController 를 거쳐 적용 -> RC 화면과 같은 규칙
 *   (LOCK / 전원 Off / 자동 / 취침) 이고, 변경은 다음 PLC 응답으로 실제 MAIN 에 전달
 * - 응답은 적용 후 상태로 만든다
 * rs485_link 태스크에서 호출 (UI / PLC 를 막지 않음).
 */
class Rs485Controller {
public:
	static Rs485Controller& instance();

	/**
	 * @brief 체크섬 확인된 요청 1개 처리. 우리 주소이고 응답할 헤더면 out 채우고 true
	 */
	bool handleRequest(const RS485::Frame& req, RS485::Frame& out);

	/**
	 * @brief 현재 응답하는 개별 ID
	 */
	uint8_t myId();

private:
	Rs485Controller() {}

	void applyE5(const RS485::Frame& req);
	void applyE7(const RS485::Frame& req);
	void applyC5(const RS485::Frame& req);
};

#endif
