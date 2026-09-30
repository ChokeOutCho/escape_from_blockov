#pragma once
////////////////////////////////////////////////////////////////////////
// 입장 대기 Content (game-spec 6.1, 9.2)
//  - 접속 직후 여기로 이동. PT_CS_ENTER_GAME을 10초 안에 받아야 한다.
//  - 버전·이름 검증 → 방 예약(채우기 우선) → GamePlayer 생성 → Move_Content(room)
////////////////////////////////////////////////////////////////////////
#include <random>
#include <unordered_map>
#include <vector>
#include "NetLibBridge.h"
#include "GameConfig.h"
#include "BattleContent.h"

class EntryContent : public NetLib_Content
{
public:
	EntryContent(const GameConfig& cfg, const std::vector<BattleContent*>& rooms);

	void OnBegin() override;
	void OnUpdate(float deltaTime) override;
	void OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen) override;
	void OnEnter(unsigned long long sessionHandle, void* completionKey) override;
	void OnLeave(unsigned long long sessionHandle) override;
	void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) override;

	int WaitingCount() const { return (int)m_waitingCount; }

	// 이름 정규화: 제어문자 제거, 양끝 공백 제거, 비면 Guest#### (game-spec 0장 닉네임 규칙)
	static void NormalizeName(const char16_t* in, char16_t* out, std::mt19937& rng);

private:
	struct Waiting
	{
		uint32_t enterTime;
		bool closing;       // 거절 응답 후 끊기 대기
	};
	struct PendingDisconnect { unsigned long long handle; uint32_t at; };

	void Reject(unsigned long long h, uint8_t result, uint32_t now);
	void KickTimeout(unsigned long long h, uint32_t now);
	BattleContent* ReserveRoom();

	const GameConfig& m_cfg;
	std::vector<BattleContent*> m_rooms;
	std::unordered_map<unsigned long long, Waiting> m_waiting;
	std::vector<PendingDisconnect> m_pending;
	std::mt19937 m_rng;
	volatile long m_waitingCount = 0;
	uint32_t m_lastCheck = 0;
};
