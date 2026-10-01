#pragma once
////////////////////////////////////////////////////////////////////////
// 더미 플레이어 1명: 연결 상태(Network가 사용) + 게임 상태/AI
//  - 모든 멤버는 lock(SRWLOCK)으로 보호. IOCP 워커(수신·송신 완료)와 로직 스레드(Tick)가 공유
//  - 객체는 프로그램 종료까지 해제하지 않는다 (진행 중 I/O가 있어도 안전)
////////////////////////////////////////////////////////////////////////
#include "Common.h"
#include <vector>
#include <unordered_map>
#include <random>

enum class NetState : uint8_t { Idle, Connecting, Connected, Closing };
enum class GameState : uint8_t { None, Entering, InGame, Dead };

enum IoType : int { IO_CONNECT = 1, IO_RECV = 2, IO_SEND = 3 };
struct IoCtx
{
	OVERLAPPED ov;
	int type;
};

struct RemotePlayer
{
	float x, z, vx, vz;
	uint32_t t;         // 마지막 수신 시각 (로컬 ms)
};

struct PendingHit
{
	uint32_t due;       // 탄이 도착하는 로컬 시각 → 이때 보고
	uint32_t shotSeq;
	uint32_t targetId;
	float hx, hz;
	uint8_t pellet;
};

struct WeaponInfo
{
	uint8_t id = 0;
	uint16_t damage = 0;
	float range = 0;
	float speed = 0;
	uint16_t intervalMs = 0;
	float radius = 0;
	float spreadDeg = 0;
	uint8_t pellets = 1;
	uint16_t durability = 0;
	uint8_t slot = 2;
};

struct AirdropInfo
{
	uint32_t id;
	float x, z;
};

class Dummy
{
public:
	void Init(int index);

	// 로직 스레드 (lock 보유 상태에서 호출)
	void Tick(uint32_t now);

	// Network 콜백 (lock 보유 상태에서 호출)
	void OnConnected(uint32_t now);
	void OnPacket(const uint8_t* payload, int len, uint32_t now);
	void OnClosed(uint32_t now, bool intended);
	void LogConnectFail(const char* reason, int err);     // 소켓 생성 단계 실패 (lock 보유)

	// 퇴장 모드에서 빠진 더미를 다시 활성화 (lock 보유)
	void ClearRetired() { m_retired = false; }

	// 대시보드용 (lock 없이 읽는 대략값)
	NetState Net() const { return net; }
	GameState Game() const { return game; }
	float Weakness() const { return m_weak; }

public:
	int index = 0;
	SRWLOCK lock;

	// ---- 네트워크 (Network 전용) ----
	SOCKET sock = INVALID_SOCKET;
	volatile NetState net = NetState::Idle;
	int ioCount = 0;
	bool closeIntended = false;
	bool closedWhileConnecting = false;
	const char* closeReason = nullptr;    // 로그용: 첫 종료 원인
	int closeError = 0;
	uint8_t lastKick = 0;                 // 마지막으로 받은 SC_KICK 사유
	uint32_t connectedAt = 0, lastRecvAt = 0;
	IoCtx connCtx{}, recvCtx{}, sendCtx{};
	static const int RECV_BUF = 8192;
	char recvBuf[RECV_BUF];
	int recvLen = 0;
	std::vector<char> sendQ, sendInflight;
	bool sending = false;

private:
	void Send(PacketWriter& w);
	void GameTick(uint32_t now);
	void Wander(uint32_t now, float dt);
	void PickHeading(uint32_t now);
	void SelectTarget(uint32_t now);
	void Fire(uint32_t now, float tx, float tz, float tvx, float tvz);
	void FlushHits(uint32_t now);
	void SendMoveIfNeeded(uint32_t now);
	void Die(uint32_t now);
	bool TryRoll(uint32_t now, float dx, float dz);
	bool MoveStep(float dt);
	void CombatMove(uint32_t now, float dt, float dx, float dz, float dist, float range);
	bool UpdateRoll(uint32_t now);
	std::string Name() const;            // 로그용 (UTF-8)
	std::u16string Name16() const;       // 프로토콜용
	bool LootTick(uint32_t now, float dt);
	void ResetLoot() { m_lootId = 0; m_lootStage = 0; m_lootOpenSent = false; }
	void EquipFromInventory();
	const char* StateName(GameState g) const;
	uint32_t ServerNow(uint32_t now) const { return now + (uint32_t)m_clockOffset; }
	float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
	int RandInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(m_rng); }

	volatile GameState game = GameState::None;
	bool m_retired = false;         // 퇴장 모드에서 사망 → 다시 접속하지 않음
	uint32_t m_reconnectAt = 0;
	uint32_t m_enterDeadline = 0;
	uint32_t m_deadAt = 0;

	// 내 캐릭터
	uint32_t m_myId = 0;
	float m_x = 0, m_z = 0, m_vx = 0, m_vz = 0, m_aim = 0;
	float m_moveSpeed = 12.0f, m_radius = 0.5f, m_sprintMul = 1.2f;
	uint8_t m_weaponId = 0;
	WeaponInfo m_weapon;
	bool m_haveWeapon = false;

	// 전송 상태
	uint16_t m_moveSeq = 0;
	uint32_t m_shotSeq = 0;
	uint32_t m_lastTick = 0, m_lastMoveSend = 0, m_nextTurn = 0, m_nextFire = 0;
	uint32_t m_nextPing = 0, m_nextHeartbeat = 0, m_nextTargetScan = 0;
	int m_pingsSent = 0;
	float m_lastSentVx = 0, m_lastSentVz = 0, m_lastSentAim = 0;
	bool m_forceMove = false;

	// 구르기 (game-spec 4.2, 17.5)
	bool m_rolling = false;
	uint32_t m_rollStart = 0, m_rollReadyAt = 0, m_nextWanderRollCheck = 0;
	float m_rollFromX = 0, m_rollFromZ = 0, m_rollToX = 0, m_rollToZ = 0;

	// 시각 동기화 (서버 시각 = 로컬 + offset)
	int32_t m_clockOffset = 0;
	int m_bestRtt = 1 << 30;
	uint32_t m_bestRttAt = 0;

	// 시야 / 교전
	std::unordered_map<uint32_t, RemotePlayer> m_remotes;
	uint32_t m_targetId = 0;
	std::vector<PendingHit> m_hits;

	// 강도 (game-spec 17.5)
	float m_weak = 0;
	uint32_t m_lastTargetId = 0;
	uint32_t m_firstShotAt = 0;

	// 무기 (game-spec 6.1): 무기표 전체, 기본 무기 id, 인벤토리
	WeaponInfo m_defs[8];
	int m_defCount = 0;
	uint8_t m_pistolId = 1;
	uint8_t m_equipped = 2, m_specialId = 0;
	uint16_t m_specialDur = 0;
	bool m_switchSent = false;
	const WeaponInfo* FindDef(uint8_t id) const { for (int i = 0; i < m_defCount; i++) if (m_defs[i].id == id) return &m_defs[i]; return nullptr; }

	// 에어드랍 특수 무기 획득 (game-spec 17.5)
	std::vector<AirdropInfo> m_airdrops;
	std::vector<uint32_t> m_lootIgnore;
	uint32_t m_lootId = 0;
	int m_lootStage = 0;                // 0 없음, 1 이동, 2 정지 후 열기 대기
	uint32_t m_lootGiveUpAt = 0, m_lootStopAt = 0, m_lootOpenAt = 0, m_lootDetourUntil = 0;
	bool m_lootOpenSent = false;

	// 교전 중 이동 (game-spec 17.5)
	float m_strafeSide = 1.0f;
	uint32_t m_nextStrafeSwitch = 0;

	std::mt19937 m_rng;
};
