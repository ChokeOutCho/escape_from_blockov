#pragma once
////////////////////////////////////////////////////////////////////////
// 더미 클라이언트 공용: 전역 설정/상태, 통계, 패킷 작성·읽기 도우미
////////////////////////////////////////////////////////////////////////
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <timeapi.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "DummyConfig.h"
#include "../GameServer/GameProtocol.h"
#include "../GameServer/ObstacleMap.h"

// NetLib 와이어 헤더: | Code(0x77) | Len(2) | RandKey(0) | CheckSum(0) | Payload |
const uint8_t NET_HEADER_CODE = 0x77;
const int NET_HEADER_SIZE = 5;
const int NET_MAX_PAYLOAD = 512;

inline uint32_t NowMs() { return timeGetTime(); }
inline int32_t TimeDiff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

////////////////////////////////////////////////////////////////////////
// 통계 (모든 스레드에서 원자적으로 누적, 대시보드가 1초마다 차분)
////////////////////////////////////////////////////////////////////////
struct Stats
{
	std::atomic<long long> sendBytes{ 0 }, recvBytes{ 0 }, sendPkts{ 0 }, recvPkts{ 0 };
	std::atomic<long long> connectOk{ 0 }, connectFail{ 0 }, disconnects{ 0 }, unexpectedDisconnects{ 0 };
	std::atomic<long long> enterOk{ 0 }, enterFull{ 0 }, enterOther{ 0 }, enterTimeout{ 0 };
	std::atomic<long long> corrections{ 0 };
	std::atomic<long long> kicks[5]{};          // [1]타임아웃 [2]잘못된 패킷 [3]치트 의심 [4]서버 종료
	std::atomic<long long> shots{ 0 }, hitsReported{ 0 }, hitsConfirmed{ 0 }, deaths{ 0 }, kills{ 0 };
	std::atomic<long long> rttSum{ 0 }, rttCount{ 0 };
	std::atomic<int> rttMax{ 0 };

	void AddRtt(int ms)
	{
		rttSum += ms;
		rttCount++;
		int m = rttMax.load();
		while (ms > m && !rttMax.compare_exchange_weak(m, ms)) {}
	}
};

extern Stats g_stats;
extern DummyConfig g_cfg;
extern ObstacleMap g_map;
extern std::atomic<int> g_targetCount;      // 목표 인원 (앞 번호부터 활성)
extern std::atomic<bool> g_fireEnabled;
extern std::atomic<bool> g_reconnectMode;   // true: 사망 시 재접속, false: 퇴장
extern std::atomic<int> g_connectTokens;    // 초당 접속 수 제한용 토큰

inline bool TakeConnectToken()
{
	if (g_connectTokens.fetch_sub(1) > 0) return true;
	g_connectTokens.fetch_add(1);
	return false;
}

////////////////////////////////////////////////////////////////////////
// 패킷 작성 (헤더 포함, 리틀 엔디안). 512B 넘으면 무시(작성 오류 방지)
////////////////////////////////////////////////////////////////////////
class PacketWriter
{
public:
	explicit PacketWriter(uint16_t type) { W16(type); }

	void W8(uint8_t v) { Put(&v, 1); }
	void W16(uint16_t v) { Put(&v, 2); }
	void W32(uint32_t v) { Put(&v, 4); }
	void WF(float v) { Put(&v, 4); }
	void WName(const std::string& ascii)
	{
		for (int i = 0; i < NAME_LEN; i++)
			W16(i < (int)ascii.size() ? (uint16_t)(unsigned char)ascii[i] : 0);
	}

	// 헤더를 채운 뒤 전체 바이트 반환
	const char* Data()
	{
		m_buf[0] = (char)NET_HEADER_CODE;
		uint16_t len = (uint16_t)m_len;
		memcpy(m_buf + 1, &len, 2);
		m_buf[3] = 0;
		m_buf[4] = 0;
		return m_buf;
	}
	int Size() const { return NET_HEADER_SIZE + m_len; }
	bool Ok() const { return !m_overflow; }

private:
	void Put(const void* p, int n)
	{
		if (m_len + n > NET_MAX_PAYLOAD) { m_overflow = true; return; }
		memcpy(m_buf + NET_HEADER_SIZE + m_len, p, n);
		m_len += n;
	}
	char m_buf[NET_HEADER_SIZE + NET_MAX_PAYLOAD];
	int m_len = 0;
	bool m_overflow = false;
};

////////////////////////////////////////////////////////////////////////
// 페이로드 읽기 (범위 밖이면 0 반환 + Ok() false)
////////////////////////////////////////////////////////////////////////
class PacketReader
{
public:
	PacketReader(const uint8_t* p, int len) : m_p(p), m_len(len) {}
	uint8_t U8() { uint8_t v = 0; Get(&v, 1); return v; }
	uint16_t U16() { uint16_t v = 0; Get(&v, 2); return v; }
	uint32_t U32() { uint32_t v = 0; Get(&v, 4); return v; }
	float F() { float v = 0; Get(&v, 4); return v; }
	void Skip(int n) { if (m_pos + n > m_len) { m_ok = false; m_pos = m_len; } else m_pos += n; }
	int Remain() const { return m_len - m_pos; }
	bool Ok() const { return m_ok; }

private:
	void Get(void* out, int n)
	{
		if (m_pos + n > m_len) { m_ok = false; m_pos = m_len; return; }
		memcpy(out, m_p + m_pos, n);
		m_pos += n;
	}
	const uint8_t* m_p;
	int m_len;
	int m_pos = 0;
	bool m_ok = true;
};
