#pragma once
////////////////////////////////////////////////////////////////////////
// 플레이어 상태 + 위치 이력(game-spec 9.5) + 사격 기록(10.2)
// EntryContent에서 생성 → Move_Content의 completionKey로 BattleContent에 전달 → BattleContent가 소유/삭제
////////////////////////////////////////////////////////////////////////
#include <cstdint>
#include <cstring>
#include <cmath>
#include "GameProtocol.h"

class PositionHistory
{
public:
	static const int CAPACITY = 64;

	struct Entry { uint32_t t; float x, z, vx, vz; };

	void Clear() { m_head = 0; m_count = 0; }
	int Count() const { return m_count; }

	void Add(uint32_t t, float x, float z, float vx, float vz)
	{
		m_buf[m_head] = { t, x, z, vx, vz };
		m_head = (m_head + 1) % CAPACITY;
		if (m_count < CAPACITY) m_count++;
	}

	const Entry& At(int i) const   // 0 = 가장 오래된 기록
	{
		return m_buf[(m_head - m_count + i + CAPACITY * 2) % CAPACITY];
	}

	uint32_t LastTime() const { return m_count ? At(m_count - 1).t : 0; }

	// 시각 t의 위치. 가장 오래된 기록 이전이면 false (판정 불가)
	bool PosAt(uint32_t t, float& outX, float& outZ) const
	{
		if (m_count == 0) return false;
		const Entry& oldest = At(0);
		if ((int32_t)(t - oldest.t) < 0) return false;

		const Entry& newest = At(m_count - 1);
		int32_t sinceNewest = (int32_t)(t - newest.t);
		if (sinceNewest >= 0)
		{
			float dt = (float)(sinceNewest > 200 ? 200 : sinceNewest) / 1000.0f;
			outX = newest.x + newest.vx * dt;
			outZ = newest.z + newest.vz * dt;
			return true;
		}
		for (int i = m_count - 2; i >= 0; i--)
		{
			const Entry& a = At(i);
			if ((int32_t)(t - a.t) >= 0)
			{
				const Entry& b = At(i + 1);
				int32_t span = (int32_t)(b.t - a.t);
				float k = span > 0 ? (float)(int32_t)(t - a.t) / (float)span : 0.0f;
				outX = a.x + (b.x - a.x) * k;
				outZ = a.z + (b.z - a.z) * k;
				return true;
			}
		}
		outX = oldest.x;
		outZ = oldest.z;
		return true;
	}

private:
	Entry m_buf[CAPACITY];
	int m_head = 0;
	int m_count = 0;
};

struct ShotRecord
{
	static const int MAX_PELLETS = 8;
	static const int MAX_TARGETS = 4;       // 1 + Pierce(≤3)

	bool valid;
	uint32_t seq;
	uint8_t weaponId;
	float ox, oz, dx, dz;
	uint32_t viewTime;      // 클램프 후
	uint32_t recvTime;
	uint8_t hitCount[MAX_PELLETS];
	uint32_t hitTargets[MAX_PELLETS][MAX_TARGETS];
};

// 기간 내 위반 횟수 카운터
struct ViolationCounter
{
	uint32_t windowStart = 0;
	int count = 0;

	// 위반 1회 추가. 기간 내 한도 도달 시 true
	bool Add(uint32_t now, int windowMs, int limit)
	{
		if (count == 0 || (int32_t)(now - windowStart) > windowMs)
		{
			windowStart = now;
			count = 0;
		}
		return ++count >= limit;
	}
};

enum class PlayerState : uint8_t { Alive, Dead };

class GamePlayer
{
public:
	static const int SHOT_RING = 64;

	explicit GamePlayer(unsigned long long sessionHandle) : sessionHandle(sessionHandle)
	{
		memset(name, 0, sizeof(name));
		memset(shots, 0, sizeof(shots));
	}

	const unsigned long long sessionHandle;
	uint32_t playerId = 0;
	char16_t name[NAME_LEN];

	PlayerState state = PlayerState::Alive;
	float x = 0, z = 0, vx = 0, vz = 0, aim = 0;
	uint16_t hp = 0, maxHp = 0;
	uint8_t weaponId = 0;

	uint32_t score = 0;
	uint32_t kills = 0;
	uint32_t scoreReachedTick = 0;
	uint32_t enterTime = 0;
	uint32_t lastRecv = 0;
	uint32_t deadTime = 0;
	bool disconnecting = false;     // 킥/사망 후 끊기 예약됨 → 입력 무시

	int sectorX = -1, sectorY = -1;

	float moveBudget = 0;
	uint32_t lastShotSeq = 0;
	float fireTokens = 3;

	// v6 인벤토리 (game-spec 19.1)
	uint8_t equipped = SLOT_PISTOL;     // 1 특수 총, 2 권총
	uint8_t specialWeaponId = 0;        // 0 = 없음
	uint16_t specialDurability = 0;
	uint8_t bandages = 0;

	// 구르기 (19.3)
	uint32_t rollUntil = 0;             // 구르기 끝 시각 (이 전의 CS_MOVE 무시)
	uint32_t rollReadyAt = 0;           // 다음 구르기 가능 시각
	bool rolling = false;
	bool rollCheckPending = false;      // 구르기 후 첫 CS_MOVE에서 도착점 차이 검사

	// 붕대 (19.4)
	uint32_t bandageUntil = 0;
	bool usingBandage = false;

	// 상호작용 (19.5)
	uint32_t lastMovedTime = 0;
	uint32_t openContainerId = 0;

	ViolationCounter moveViolations;
	ViolationCounter cheatViolations;

	PositionHistory history;
	ShotRecord shots[SHOT_RING];

	bool IsAlive() const { return state == PlayerState::Alive; }

	ShotRecord* FindShot(uint32_t seq)
	{
		ShotRecord& r = shots[seq % SHOT_RING];
		return (r.valid && r.seq == seq) ? &r : nullptr;
	}

	void RecordHistory(uint32_t now) { history.Add(now, x, z, vx, vz); }
};
