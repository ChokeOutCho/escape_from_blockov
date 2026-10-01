#pragma once
////////////////////////////////////////////////////////////////////////
// 전투 방 1개 (game-spec 7, 10.3~10.5, 11). 플레이어·섹터·랭킹·전투 판정·아이템·에어드랍·파괴 가능 엄폐물 상태를 모두 소유.
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
	BattleContent(int roomNo, const GameConfig& cfg, const WeaponTable& weapons, const ObstacleMap& obstacles);
	~BattleContent();

	// EntryContent에서 호출 (다른 스레드)
	bool TryReserve();
	void Unreserve() { m_reserved.fetch_sub(1); }
	int Reserved() const { return m_reserved.load(); }
	int PlayerCount() const { return m_playerCount.load(); }
	int RoomNo() const { return m_roomNo; }
	// 서버 전체 접속 인원 (모든 방 합계). 입장 시 +1, 연결 해제 시 -1
	static int OnlineTotal() { return s_online.load(); }

	void OnBegin() override;
	void OnUpdate(float deltaTime) override;
	void OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen) override;
	void OnEnter(unsigned long long sessionHandle, void* completionKey) override;
	void OnLeave(unsigned long long sessionHandle) override;
	void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) override;

private:
	struct PendingDisconnect { unsigned long long handle; uint32_t at; };
	struct RankEntry { uint32_t id; uint32_t score; };

	// 가방·에어드랍 (game-spec 6.3~6.5)
	struct Container
	{
		uint32_t id;
		uint8_t type;           // CONTAINER_BAG / CONTAINER_AIRDROP
		float x, z;
		int sx, sy;
		uint8_t specialId;
		uint16_t durability;
		uint8_t bandages;
		uint32_t expireAt;      // 가방만 (에어드랍은 비면 제거)
	};

	// 수신 처리
	void HandleMove(GamePlayer* p, PayloadReader& r, uint32_t now);
	void HandleFire(GamePlayer* p, PayloadReader& r, uint32_t now);
	void HandleHitReport(GamePlayer* p, PayloadReader& r, int count, uint32_t now);
	void HandleRoll(GamePlayer* p, PayloadReader& r, uint32_t now);
	void HandleSwitch(GamePlayer* p, uint8_t slot, uint32_t now);
	void HandleBandage(GamePlayer* p, uint32_t now);
	void HandleOpen(GamePlayer* p, uint32_t id, uint32_t now);
	void HandleTake(GamePlayer* p, uint32_t id, uint8_t item, uint32_t now);
	// 0 = 통과, >0 = 위반(ViolationKind, 부정 카운트), <0 = 정상 플레이에서도 생기는 거부(세지 않음)
	int ValidateHit(GamePlayer* shooter, uint32_t shotSeq, uint8_t pellet, uint32_t targetId,
	                float hx, float hz, uint32_t now, GamePlayer** outTarget, const WeaponDef** outWeapon);
	int ValidateCoverHit(GamePlayer* shooter, uint32_t shotSeq, uint8_t pellet, int coverId,
	                     float hx, float hz, uint32_t now, const WeaponDef** outWeapon);

	// 파괴 가능 엄폐물 (game-spec 3.6)
	void DamageCover(int coverId, uint16_t damage, uint32_t now);
	void UpdateCovers(uint32_t now);
	void SendCoverStates(const std::vector<unsigned long long>& hs, const std::vector<uint16_t>& ids);
	bool CoverOccupied(int coverId) const;

	// 게임 규칙
	void ChooseSpawn(float& outX, float& outZ);     // 주변 3x3 인원이 가장 적은 섹터 안 무작위 위치
	void ChangeSector(GamePlayer* p, int nsx, int nsy);
	void ApplyDamage(GamePlayer* shooter, GamePlayer* victim, uint32_t shotSeq, uint16_t damage, uint32_t now);
	void Kill(GamePlayer* victim, GamePlayer* killer, uint32_t now);
	void Kick(GamePlayer* p, uint8_t reason, uint32_t now);
	void ScheduleDisconnect(GamePlayer* p, uint32_t at);
	void RemoveFromWorld(GamePlayer* p);
	void UpdateRanking(bool force);
	void ComputeTop3(std::vector<RankEntry>& out) const;
	void CountCheat(GamePlayer* p, uint32_t now, uint8_t kind);
	void CountMoveViolation(GamePlayer* p, uint32_t now, uint8_t kind);
	void LogKickDetail(GamePlayer* p, uint8_t reason);
	void UpdateOnlineCount(uint32_t now);

	// v6 아이템·컨테이너
	void CancelBandage(GamePlayer* p) { p->usingBandage = false; p->bandageUntil = 0; }
	void SendInventory(GamePlayer* p);
	void SendHp(GamePlayer* p);                                     // 3x3
	float FireTokenCap(const WeaponDef* w) const;
	void CreateBag(GamePlayer* victim, uint32_t now);
	bool PickAirdropPos(const std::vector<std::pair<int, int>>& taken, float& x, float& z, int& groupAlive);
	void CreateAirdrop(float x, float z, int groupAlive);
	void PlanAirdrops(std::vector<std::pair<float, float>>& out, std::vector<int>& groupAlive);
	void AirdropRound(uint32_t now);         // 예고 없이 즉시 (빈 방 첫 입장)
	void ForecastAirdrops(uint32_t now);     // 투하 예정 위치 결정 + 방 전체 공개
	void SendForecast(const std::vector<unsigned long long>& hs);
	int AirdropCap() const;
	void RemoveContainer(uint32_t id);
	void SendContents(GamePlayer* to, const Container& c);
	void BroadcastContents(const Container& c);
	void SendAirdrop(const std::vector<unsigned long long>& hs, const Container& c, bool isNew);
	void CollectBagsInView(int sx, int sy, std::vector<uint32_t>& out) const;
	void SendContainerCreate(GamePlayer* to, const std::vector<uint32_t>& ids);
	void SendContainerDelete(GamePlayer* to, const std::vector<uint32_t>& ids);
	void AllHandles(std::vector<unsigned long long>& out) const;
	void UpdateContainers(uint32_t now);

	// 송신
	void SendTo(GamePlayer* p, Packet* netPacket);                 // Packet::NetAlloc() 패킷, 소유권 이전
	void Multicast(const std::vector<unsigned long long>& handles, Packet* packet);   // Packet::Alloc() 패킷, 호출 후 Free
	void CollectView(GamePlayer* center, GamePlayer* exclude, std::vector<GamePlayer*>& out) const;
	void CollectViewOfSector(int sx, int sy, std::vector<unsigned long long>& out) const;
	void Handles(const std::vector<GamePlayer*>& players, std::vector<unsigned long long>& out) const;
	void WriteCreateEntry(Packet* pkt, GamePlayer* p, uint8_t flags = 0) const;
	void SendCreateList(GamePlayer* to, const std::vector<GamePlayer*>& list);
	void SendDeleteList(GamePlayer* to, const std::vector<GamePlayer*>& list);
	Packet* BuildCreateOne(GamePlayer* p, uint8_t flags = 0) const;        // Alloc
	Packet* BuildDeleteOne(GamePlayer* p) const;        // Alloc
	Packet* BuildRanking(const std::vector<RankEntry>& top) const;   // Alloc
	void SendEnterSequence(GamePlayer* p);

	GamePlayer* FindById(uint32_t id) const;

	const int m_roomNo;
	const GameConfig& m_cfg;
	const WeaponTable& m_weapons;
	const ObstacleMap& m_obstacles;

	std::atomic<int> m_reserved{ 0 };
	std::atomic<int> m_playerCount{ 0 };

	std::unordered_map<unsigned long long, GamePlayer*> m_players;     // sessionHandle → player
	std::unordered_map<uint32_t, GamePlayer*> m_byId;
	SectorMap m_sectors;
	uint32_t m_nextPlayerId = 1;
	std::mt19937 m_rng;

	std::unordered_map<uint32_t, Container> m_containers;
	std::vector<std::vector<uint32_t>> m_bagCells;     // 섹터별 가방 id (시야 계산)
	uint32_t m_nextContainerId = 1;
	uint32_t m_nextAirdropAt = 0;           // 다음 투하 시각
	bool m_airdropActive = false;
	bool m_forecastActive = false;          // 다음 투하 위치가 정해져 공개됨
	std::vector<std::pair<float, float>> m_forecastPos;
	std::vector<int> m_forecastAlive;

	// 파괴 가능 엄폐물 상태 (id별)
	std::vector<uint8_t> m_coverHp;
	std::vector<uint8_t> m_coverDestroyed;
	std::vector<uint32_t> m_coverDestroyedAt;
	std::vector<uint16_t> m_destroyedCovers;    // 파괴된 id 목록 (재생 검사·입장 동기화)
	uint32_t m_lastCoverCheck = 0;
	uint32_t m_lastContainerCheck = 0;

	std::vector<PendingDisconnect> m_pending;
	std::vector<RankEntry> m_lastTop;
	static std::atomic<int> s_online;
	int m_lastSentOnline = -1;
	uint32_t m_lastOnlineSend = 0;
	uint32_t m_lastSecondTick = 0;
	uint32_t m_lastUpdateTime = 0;

	// 재사용 스크래치 버퍼
	std::vector<GamePlayer*> m_tmpPlayers;
	std::vector<GamePlayer*> m_tmpPlayers2;
	std::vector<unsigned long long> m_tmpHandles;
};
