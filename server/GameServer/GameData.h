#pragma once
////////////////////////////////////////////////////////////////////////
// 불변 게임 데이터: 무기 테이블(weapons.txt), 스폰 목록(spawns.txt), 맵 상수.
// 로드 후 읽기 전용 → 모든 Content가 락 없이 공유.
////////////////////////////////////////////////////////////////////////
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

#include "ObstacleMap.h"

struct WeaponDef
{
	uint8_t id = 0;
	std::string name;
	uint16_t damage = 0;
	float range = 0;
	float projectileSpeed = 0;
	uint16_t fireIntervalMs = 0;
	float projectileRadius = 0;
	uint16_t magazineSize = 0;
	uint16_t reloadMs = 0;
	float spreadDeg = 0;
	uint8_t pellets = 1;
	uint8_t pierce = 0;
};

class WeaponTable
{
public:
	// 형식(공백 구분, # 주석):
	// id name damage range speed intervalMs radius magazine reloadMs spreadDeg pellets pierce
	// maxRange: 시야 보장 거리(섹터 크기 × 시야 반경). 넘으면 잘라낸다 (game-spec 3.1)
	bool Load(const char* path, float maxRange);
	const WeaponDef* Find(uint8_t id) const;
	const std::vector<WeaponDef>& All() const { return m_defs; }

private:
	std::vector<WeaponDef> m_defs;
};

struct SpawnPoint
{
	int sx, sy;
	float x, z;     // 섹터 중심
};

class SpawnTable
{
public:
	// 형식: 한 줄에 "sx sy" (# 주석)
	bool Load(const char* path);
	const std::vector<SpawnPoint>& All() const { return m_spawns; }

private:
	std::vector<SpawnPoint> m_spawns;
};
