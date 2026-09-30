#pragma once
////////////////////////////////////////////////////////////////////////
// 비정상 이벤트 파일 로그 (logs/dummy_YYYYMMDD_HHMMSS.log)
//  - 비정상 연결 끊김, 접속 실패, 입장 거부/타임아웃을 한 줄씩 기록
//  - 여러 스레드에서 호출 → 내부 락. 1초마다(또는 종료 시) flush
////////////////////////////////////////////////////////////////////////
#include <atomic>
#include <cstdio>
#include <string>

class EventLog
{
public:
	bool Open(const std::string& header);
	void Close();
	void Write(const char* type, const char* fmt, ...);
	void Flush();

	const std::string& Path() const { return m_path; }
	long long Count(int kind) const { return m_counts[kind].load(); }

	enum Kind { DISCONNECT = 0, CONNECT_FAIL = 1, ENTER_FAIL = 2, KIND_MAX = 3 };
	void Add(Kind k) { m_counts[k]++; }

	// Windows 오류 코드 → 설명 문자열 (시스템 언어)
	static std::string ErrorText(int err);

private:
	FILE* m_fp = nullptr;
	std::string m_path;
	std::atomic<long long> m_counts[KIND_MAX]{};
};

extern EventLog g_eventLog;
