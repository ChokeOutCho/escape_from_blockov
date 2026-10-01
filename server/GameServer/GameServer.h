#pragma once
////////////////////////////////////////////////////////////////////////
// 게임 서버 (game-spec 10.1). EntryContent 1개 + BattleContent N개를 생성·등록.
// 자체 암호화는 사용하지 않는다(opt_encryption = nullptr).
////////////////////////////////////////////////////////////////////////
#include <vector>
#include "NetLibBridge.h"
#include "GameConfig.h"
#include "GameData.h"
#include "EntryContent.h"
#include "BattleContent.h"

class GameServer : public NetLib_Server
{
public:
	GameServer(const GameConfig& cfg, const WeaponTable& weapons, const SpawnTable& spawns, const ObstacleMap& obstacles);

	bool OnConnectionRequest(unsigned long IP, unsigned short port) override { return true; }
	void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port) override;
	void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) override {}
	void OnRecv(unsigned long long sessionHandle, Packet* packet) override {}

	EntryContent* Entry() { return m_entry; }
	const std::vector<BattleContent*>& Rooms() const { return m_rooms; }

private:
	const GameConfig& m_cfg;
	EntryContent* m_entry = nullptr;
	std::vector<BattleContent*> m_rooms;
};
