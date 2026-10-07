#pragma once
#include <windows.h>
#pragma comment(lib, "Winmm.lib")

////////////////////////////////////////////////////////////////////////
// escape_from_blockov 게임 프로토콜 (GAME_SPEC.md 7장)
//  - 리틀 엔디안, pack(1), NetHeader(5B) 뒤 Payload = WORD Type + 본문
//  - C->S 3000~3099, S->C 3100~3199
//  - 페이로드 최대 PAYLOAD_LEN_DEFAULT(512)
////////////////////////////////////////////////////////////////////////

const UINT32 GAME_PROTOCOL_VERSION = 2;

enum en_GAME_PACKET_TYPE : WORD
{
	//------------------------------------------------------------
	// 레이턴시 측정 / 시각 동기화 요청. 클라가 2초마다 전송
	//	{
	//		WORD	Type
	//		UINT32	ClientTimeMs		// 클라 로컬 ms. SC_PONG으로 그대로 되돌려 받음
	//	}
	//------------------------------------------------------------
	CS_PING = 3004,

	//------------------------------------------------------------
	// 하트비트. 클라가 60초마다 전송 (백그라운드 탭에서도 동작하는 타이머에서 송신)
	// 서버는 HEARTBEAT_TIME_OUT(3분) 동안 아무 패킷도 받지 못하면 끊는다.
	//	{
	//		WORD	Type
	//	}
	//------------------------------------------------------------
	CS_HEARTBEAT = 3005,

	//------------------------------------------------------------
	// CS_PING 응답
	//	{
	//		WORD	Type
	//		UINT32	ClientTimeMs		// CS_PING 값 그대로
	//		UINT32	ServerTimeMs		// 응답 생성 시각 (GetServerTimeMs)
	//	}
	//------------------------------------------------------------
	SC_PONG = 3113,
};

// 서버 시각(ms) = 서버 프로세스 시작 기준 경과 ms. UINT32 wrap 허용(비교는 부호 있는 차이로)
inline UINT32 GetServerTimeMs()
{
	static const DWORD s_startTick = timeGetTime();
	return (UINT32)(timeGetTime() - s_startTick);
}
