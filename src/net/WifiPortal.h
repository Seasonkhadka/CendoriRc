#ifndef __WIFIPORTAL_H__
#define __WIFIPORTAL_H__

/**
 * @brief 설정 모드용 웹 포털 (휴대폰이 설정 AP 에 붙으면 열리는 페이지)
 *
 * - HTTP 서버: "/" 네트워크 선택 + 비밀번호 입력 페이지, "/connect" 제출
 *   그 외 주소는 "/" 로 리다이렉트 (휴대폰이 로그인 페이지로 자동 표시)
 * - DNS 서버: 모든 이름을 AP 주소로 응답 (캡티브 포털)
 * 제출된 정보는 WifiController::connect() 로 넘긴다.
 */
namespace WifiPortal {
	bool start();
	void stop();
}

#endif
