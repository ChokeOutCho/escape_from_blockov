#pragma once
////////////////////////////////////////////////////////////////////////
// NetLib 연결부.
//  - Windows 빌드: 실제 NetLib(../ContentEchoServer/NetLib) 사용
//  - GAME_STUB_NETLIB 정의 시: 리눅스 테스트용 스텁(test/StubNetLib.h) 사용
// 게임 로직 코드는 이 헤더만 include 하고 NetLib API만 사용한다.
////////////////////////////////////////////////////////////////////////
#ifdef GAME_STUB_NETLIB
#include "test/StubNetLib.h"
#else
#include "../ContentEchoServer/NetLib/NetLib_Server.h"
#include "../ContentEchoServer/NetLib/NetLib_Content.h"
#pragma comment(lib, "Winmm.lib")
#endif

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <deque>
#include <string>
#include <vector>

// 서버 시각(ms) = 서버 프로세스 시작 기준 경과 ms. UINT32 wrap 허용 → 비교는 TimeDiff로
inline uint32_t GetServerTimeMs()
{
	static const DWORD s_start = timeGetTime();
	return (uint32_t)(timeGetTime() - s_start);
}

// a - b (부호 있는 ms). wrap 안전
inline int32_t TimeDiff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

// 게임 로그: Content(워커) 스레드에서 콘솔에 직접 printf 하면 콘솔 출력이 막힐 때(선택 모드 등) 워커가 멈춘다.
// → 메모리 버퍼에 쌓고 main 스레드가 모니터링 화면 아래에 출력한다.
struct GameLogBuffer
{
	static const size_t MAX_LINES = 200;
	std::mutex lock;
	std::deque<std::string> lines;
	unsigned long long total = 0;

	static GameLogBuffer& Instance() { static GameLogBuffer b; return b; }

	void Push(const char* msg)
	{
		std::lock_guard<std::mutex> g(lock);
		lines.emplace_back(msg);
		total++;
		if (lines.size() > MAX_LINES) lines.pop_front();
	}
	// 최근 n줄 복사
	std::vector<std::string> Tail(size_t n)
	{
		std::lock_guard<std::mutex> g(lock);
		size_t start = lines.size() > n ? lines.size() - n : 0;
		return std::vector<std::string>(lines.begin() + start, lines.end());
	}
};

inline void GameLog(const char* fmt, ...)
{
	char buf[512];
	uint32_t t = GetServerTimeMs() / 1000;
	int n = snprintf(buf, sizeof(buf), "[%02u:%02u:%02u] ", t / 3600, (t / 60) % 60, t % 60);
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
	va_end(args);
	GameLogBuffer::Instance().Push(buf);
}

////////////////////////////////////////////////////////////////////////
// 직렬화 헬퍼 (SerializeBuffer의 오버로드 모호성을 피하기 위해 타입 고정)
////////////////////////////////////////////////////////////////////////
inline void W8(Packet* p, uint8_t v) { *p << (BYTE)v; }
inline void W16(Packet* p, uint16_t v) { *p << (WORD)v; }
inline void W32(Packet* p, uint32_t v) { *p << (DWORD)v; }
inline void WF(Packet* p, float v) { *p << v; }
inline void WName(Packet* p, const char16_t* name, int len)
{
	for (int i = 0; i < len; i++) *p << (WORD)name[i];
}

////////////////////////////////////////////////////////////////////////
// 수신 페이로드 리더 (길이 검사 포함). 범위를 넘으면 ok=false
////////////////////////////////////////////////////////////////////////
struct PayloadReader
{
	const char* p;
	int len;
	int pos;
	bool ok;

	PayloadReader(const char* payload, int payloadLen) : p(payload), len(payloadLen), pos(0), ok(true) {}

	template <typename T>
	T Read()
	{
		T v{};
		if (pos + (int)sizeof(T) > len) { ok = false; return v; }
		memcpy(&v, p + pos, sizeof(T));
		pos += sizeof(T);
		return v;
	}
	uint8_t U8() { return Read<uint8_t>(); }
	uint16_t U16() { return Read<uint16_t>(); }
	uint32_t U32() { return Read<uint32_t>(); }
	float F() { return Read<float>(); }
	void Name(char16_t* out, int n)
	{
		for (int i = 0; i < n; i++) out[i] = (char16_t)U16();
	}
};
