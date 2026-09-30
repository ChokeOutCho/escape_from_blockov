#pragma once
////////////////////////////////////////////////////////////////////////
// 전투 방 1개 (game-spec 6, 9.3~9.5, 10). 플레이어·섹터·랭킹·전투 판정을 모두 소유.
// Content 콜백은 단일 스레드로 실행되므로 멤버에 락이 필요 없다.
// reserved(방 배정 예약 수)만 EntryContent와 공유 → atomic.
////////////////////////////////////////////////////////////////////////
#include <atomic>
#include <random>
#include <unordered_map>
#include <vector>
#include "NetLibBridge.h"
#include "GameConfig.h"
#include "GameData.h"
#include "GamePlayer.h"
#include "SectorMap.h"

class BattleContent : public NetLib_Content
{
public:
	BattleContent(int roomNo, const GameConfig& cfg, const WeaponTable& weapons, const SpawnTable& spawns, const ObstacleMap& obstacles);
	~BattleContent();

	// EntryContent에서 호출 (다른 스레드)
	bool TryReserve();
	void Unreserve() { m_reserved.fetch_sub(1); }
	int Reserved() const { return m_reserved.load(); }
	int PlayerCount() const { return m_playerCount.load(); }
	int RoomNo() const { return m_roomNo; }

	void OnBegin() override;
	void OnUpdate(float deltaTime) override;
	void OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen) override;
	void OnEnter(unsigned long long sessionHandle, void* completionKey) override;
	void OnLeave(unsigned long long sessionHandle) override;
	void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) override;

private:
	struct PendingDisconnect { unsigned long long handle; uint32_t at; };
	struct RankEntry { uint32_t id; uint32_t score; };

	// 수신 처리
	void HandleMove(GamePlayer* p, PayloadReader& r, uint32_t now);
	void HandleFire(GamePlayer* p, PayloadReader& r, uint32_t now);
	void HandleHitReport(GamePlayer* p, PayloadReader& r, int count, uint32_t now);
	bool ValidateHit(GamePlayer* shooter, uint32_t shotSeq, uint8_t pellet, uint32_t targetId,
	                 float hx, float hz, uint32_t now, GamePlayer** outTarget, const WeaponDef** outWeapon);

	// 게임 규칙
	void ChooseSpawn(float& outX, float& outZ);
	void ChangeSector(GamePlayer* p, int nsx, int nsy);
	void ApplyDamage(GamePlayer* shooter, GamePlayer* victim, uint32_t shotSeq, uint16_t damage, uint32_t now);
	void Kill(GamePlayer* victim, GamePlayer* killer, uint32_t now);
	void Kick(GamePlayer* p, uint8_t reason, uint32_t now);
	void ScheduleDisconnect(GamePlayer* p, uint32_t at);
	void RemoveFromWorld(GamePlayer* p);
	void UpdateRanking(bool force);
	void ComputeTop3(std::vector<RankEntry>& out) const;
	void CountCheat(GamePlayer* p, uint32_t now);

	// 송신
	void SendTo(GamePlayer* p, Packet* netPacket);                 // Packet::NetAlloc() 패킷, 소유권 이전
	void Multicast(const std::vector<unsigned long long>& handles, Packet* packet);   // Packet::Alloc() 패킷, 호출 후 Free
	void CollectView(GamePlayer* center, GamePlayer* exclude, std::vector<GamePlayer*>& out) const;
	void Handles(const std::vector<GamePlayer*>& players, std::vector<unsigned long long>& out) const;
	void WriteCreateEntry(Packet* pkt, GamePlayer* p) const;
	void SendCreateList(GamePlayer* to, const std::vector<GamePlayer*>& list);
	void SendDeleteList(GamePlayer* to, const std::vector<GamePlayer*>& list);
	Packet* BuildCreateOne(GamePlayer* p) const;        // Alloc
	Packet* BuildDeleteOne(GamePlayer* p) const;        // Alloc
	Packet* BuildRanking(const std::vector<RankEntry>& top) const;   // Alloc
	void SendEnterSequence(GamePlayer* p);

	GamePlayer* FindById(uint32_t id) const;

	const int m_roomNo;
	const GameConfig& m_cfg;
	const WeaponTable& m_weapons;
	const SpawnTable& m_spawns;
	const ObstacleMap& m_obstacles;

	std::atomic<int> m_reserved{ 0 };
	std::atomic<int> m_playerCount{ 0 };

	std::unordered_map<unsigned long long, GamePlayer*> m_players;     // sessionHandle → player
	std::unordered_map<uint32_t, GamePlayer*> m_byId;
	SectorMap m_sectors;
	uint32_t m_nextPlayerId = 1;
	std::mt19937 m_rng;

	std::vector<PendingDisconnect> m_pending;
	std::vector<RankEntry> m_lastTop;
	uint32_t m_lastSecondTick = 0;
	uint32_t m_lastUpdateTime = 0;

	// 재사용 스크래치 버퍼
	std::vector<GamePlayer*> m_tmpPlayers;
	std::vector<GamePlayer*> m_tmpPlayers2;
	std::vector<unsigned long long> m_tmpHandles;
};
