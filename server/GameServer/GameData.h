#pragma once
////////////////////////////////////////////////////////////////////////
// 불변 게임 데이터: 무기 테이블(weapons.txt), 맵 상수.
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
	float jitterDeg = 0;        // 발사 방향 무작위 흔들림 ±jitterDeg (클라 적용)
	uint16_t durability = 0;    // 특수 총 최대 내구도(발사 횟수). 0 = 무한
	uint8_t slot = 2;           // 1 특수 총, 2 기본 총
};

class WeaponTable
{
public:
	// 형식(공백 구분, # 주석):
	// id name damage range speed intervalMs radius magazine reloadMs spreadDeg pellets pierce [jitterDeg durability slot]
	// (v6 열 3개는 생략 가능: 0 0 2)
	// maxRange: 시야 보장 거리(섹터 크기 × 시야 반경). 넘으면 잘라낸다 (game-spec 3.1)
	bool Load(const char* path, float maxRange);
	const WeaponDef* Find(uint8_t id) const;
	const std::vector<WeaponDef>& All() const { return m_defs; }
	// 특수 총(slot 1) 목록 (에어드랍 내용물 선택용)
	std::vector<const WeaponDef*> Specials() const
	{
		std::vector<const WeaponDef*> v;
		for (const auto& d : m_defs) if (d.slot == 1) v.push_back(&d);
		return v;
	}

private:
	std::vector<WeaponDef> m_defs;
};
