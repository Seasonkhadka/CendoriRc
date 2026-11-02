#include "WifiPortal.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "WifiConfig.h"
#include "WifiController.h"

namespace {

const char* TAG = "PORTAL";

httpd_handle_t s_server = nullptr;
std::atomic<bool> s_dnsRun(false);
TaskHandle_t s_dnsTask = nullptr;

// ---- HTML ----

std::string htmlEscape(const char* s) {
	std::string out;
	for( ; *s; s++ ) {
		switch( *s ) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&#39;"; break;
			default: out += *s; break;
		}
	}
	return out;
}

// application/x-www-form-urlencoded 값 디코딩 ('+' -> 공백, %XX)
std::string urlDecode(const char* s) {
	std::string out;
	for( ; *s; s++ ) {
		if( *s == '+' ) {
			out += ' ';
		} else if( *s == '%' && s[1] && s[2] ) {
			char hex[3] = {s[1], s[2], 0};
			out += (char)strtol(hex, nullptr, 16);
			s += 2;
		} else {
			out += *s;
		}
	}
	return out;
}

const char* PAGE_HEAD =
	"<!DOCTYPE html><html><head><meta charset='utf-8'>"
	"<meta name='viewport' content='width=device-width,initial-scale=1'>"
	"<title>CENDORI RC Wi-Fi</title><style>"
	"body{font-family:sans-serif;background:#0a0d12;color:#f3f6fa;margin:0;padding:20px}"
	"h1{font-size:20px;margin:0 0 4px}p{color:#8a93a6;font-size:14px;margin:0 0 16px}"
	"label{display:block;font-size:14px;color:#8a93a6;margin:14px 0 6px}"
	"select,input{width:100%;box-sizing:border-box;padding:12px;font-size:16px;border-radius:10px;"
	"border:1px solid #242a35;background:#161b23;color:#f3f6fa}"
	"button{width:100%;margin-top:20px;padding:14px;font-size:17px;border:0;border-radius:24px;"
	"background:#5ad1e8;color:#14181f;font-weight:bold}"
	"a{color:#5ad1e8}.box{max-width:420px;margin:0 auto}</style></head><body><div class='box'>";
const char* PAGE_TAIL = "</div></body></html>";

esp_err_t sendPage(httpd_req_t* req, const std::string& body) {
	httpd_resp_set_type(req, "text/html; charset=utf-8");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	std::string page = PAGE_HEAD;
	page += body;
	page += PAGE_TAIL;
	return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t handleRoot(httpd_req_t* req) {
	const WifiDto st = WifiController::instance().getState();
	std::string b;
	b += "<h1>CENDORI 룸 콘트롤러 Wi-Fi</h1>";
	b += "<p>연결할 Wi-Fi 를 선택하고 비밀번호를 입력하세요.<br>Choose your Wi-Fi and enter the password.</p>";
	b += "<form method='post' action='/connect'>";
	b += "<label>Wi-Fi 네트워크 (Network)</label><select name='ssid'>";
	for( uint8_t i = 0; i < st.apCount; i++ ) {
		const std::string name = htmlEscape(st.aps[i].ssid);
		b += "<option value='" + name + "'>" + name + (st.aps[i].secure ? " &#128274;" : "") + "</option>";
	}
	b += "<option value=''>직접 입력 (Other...)</option></select>";
	b += "<label>목록에 없으면 이름 입력 (Hidden network name)</label>";
	b += "<input name='other' maxlength='32' autocomplete='off'>";
	b += "<label>비밀번호 (Password)</label>";
	b += "<input name='pass' type='password' maxlength='63' autocomplete='off'>";
	b += "<button type='submit'>연결 (Connect)</button></form>";
	if( st.scanning ) b += "<p style='margin-top:14px'>검색 중... (Scanning)</p>";
	b += "<p style='margin-top:14px'><a href='/'>목록 새로고침 (Refresh)</a></p>";
	return sendPage(req, b);
}

esp_err_t handleConnect(httpd_req_t* req) {
	char body[512] = {};
	const int len = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
	int got = 0;
	while( got < len ) {
		const int r = httpd_req_recv(req, body + got, len - got);
		if( r <= 0 ) break;
		got += r;
	}
	body[got] = '\0';

	char raw[200];
	std::string ssid, other, pass;
	if( httpd_query_key_value(body, "ssid", raw, sizeof(raw)) == ESP_OK ) ssid = urlDecode(raw);
	if( httpd_query_key_value(body, "other", raw, sizeof(raw)) == ESP_OK ) other = urlDecode(raw);
	if( httpd_query_key_value(body, "pass", raw, sizeof(raw)) == ESP_OK ) pass = urlDecode(raw);
	if( !other.empty() ) ssid = other;

	if( ssid.empty() || ssid.size() > 32 || pass.size() > 63 || (!pass.empty() && pass.size() < 8) ) {
		return sendPage(req, "<h1>입력 확인 (Check input)</h1>"
							 "<p>네트워크 이름을 선택하고, 비밀번호는 8자 이상 입력하세요.<br>"
							 "Select a network; the password needs at least 8 characters.</p>"
							 "<p><a href='/'>돌아가기 (Back)</a></p>");
	}

	ESP_LOGI(TAG, "connect request '%s'", ssid.c_str());
	esp_err_t r = sendPage(req, "<h1>연결 중... (Connecting)</h1>"
								"<p>'" + htmlEscape(ssid.c_str()) + "' 에 연결합니다.<br>"
								"룸 콘트롤러 화면에서 결과를 확인하세요.<br>"
								"Check the result on the room controller screen.</p>"
								"<p>실패하면 이 페이지로 돌아와 다시 시도하세요.<br>"
								"If it fails, come back to this page and try again.</p>");
	WifiController::instance().connect(ssid, pass);
	return r;
}

// 그 외 모든 주소 -> 포털 첫 페이지 (휴대폰의 연결 확인 요청이 여기로 와서 로그인 페이지가 뜬다)
esp_err_t handleNotFound(httpd_req_t* req, httpd_err_code_t) {
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", "http://" WIFI_AP_IP "/");
	httpd_resp_send(req, nullptr, 0);
	return ESP_OK;
}

// ---- DNS: 모든 질의에 AP 주소로 응답 ----

void dnsTask(void*) {
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if( sock < 0 ) {
		ESP_LOGE(TAG, "dns socket failed");
		s_dnsTask = nullptr;
		vTaskDelete(nullptr);
		return;
	}
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct timeval tv = {1, 0};
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(53);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if( bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0 ) {
		ESP_LOGE(TAG, "dns bind failed");
		close(sock);
		s_dnsTask = nullptr;
		vTaskDelete(nullptr);
		return;
	}

	uint32_t apIp = inet_addr(WIFI_AP_IP);
	uint8_t buf[512];
	while( s_dnsRun ) {
		struct sockaddr_in from;
		socklen_t fromLen = sizeof(from);
		int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromLen);
		if( n < 12 ) continue;
		const uint16_t qd = (buf[4] << 8) | buf[5];
		// 첫 질문의 끝 찾기 (이름 라벨 + QTYPE/QCLASS)
		int p = 12;
		while( p < n && buf[p] != 0 ) p += buf[p] + 1;
		p += 5;
		if( qd == 0 || p > n ) continue;
		const uint16_t qtype = (buf[p - 4] << 8) | buf[p - 3];
		const bool answer = (qtype == 1);	// A 레코드만 응답

		buf[2] = 0x81;	// 응답, 재귀 가능
		buf[3] = 0x80;
		buf[4] = 0; buf[5] = 1;			// 질문 1개
		buf[6] = 0; buf[7] = answer ? 1 : 0;
		buf[8] = buf[9] = buf[10] = buf[11] = 0;
		int len = p;
		if( answer && len + 16 <= (int)sizeof(buf) ) {
			const uint8_t ans[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
									 (uint8_t)(apIp), (uint8_t)(apIp >> 8), (uint8_t)(apIp >> 16), (uint8_t)(apIp >> 24)};
			memcpy(buf + len, ans, sizeof(ans));
			len += sizeof(ans);
		}
		sendto(sock, buf, len, 0, (struct sockaddr*)&from, fromLen);
	}
	close(sock);
	s_dnsTask = nullptr;
	vTaskDelete(nullptr);
}

} // namespace

namespace WifiPortal {

bool start() {
	if( s_server ) return true;
	httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
	cfg.lru_purge_enable = true;
	cfg.max_open_sockets = 5;
	if( httpd_start(&s_server, &cfg) != ESP_OK ) {
		ESP_LOGE(TAG, "httpd start failed");
		s_server = nullptr;
		return false;
	}
	httpd_uri_t root = {};
	root.uri = "/";
	root.method = HTTP_GET;
	root.handler = handleRoot;
	httpd_register_uri_handler(s_server, &root);
	httpd_uri_t conn = {};
	conn.uri = "/connect";
	conn.method = HTTP_POST;
	conn.handler = handleConnect;
	httpd_register_uri_handler(s_server, &conn);
	httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handleNotFound);

	if( !s_dnsTask ) {
		s_dnsRun = true;
		xTaskCreate(dnsTask, "dns", 4096, nullptr, 4, &s_dnsTask);
	}
	ESP_LOGI(TAG, "portal started at http://%s/", WIFI_AP_IP);
	return true;
}

void stop() {
	s_dnsRun = false;	// DNS 태스크는 1초 안에 스스로 끝남
	if( s_server ) {
		httpd_stop(s_server);
		s_server = nullptr;
		ESP_LOGI(TAG, "portal stopped");
	}
}

} // namespace WifiPortal
