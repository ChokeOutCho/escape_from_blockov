#pragma once
////////////////////////////////////////////////////////////////////////
// 불변 게임 데이터: 무기 테이블(weapons.txt), 스폰 목록(spawns.txt), 맵 상수.
// 로드 후 읽기 전용 → 모든 Content가 락 없이 공유.
////////////////////////////////////////////////////////////////////////
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

namespace MapConst
{
	const float SectorSize = 64.0f;
	const int SectorCount = 100;                // 100 x 100
	const float WorldSize = SectorSize * SectorCount;   // 6400
	const int WorldCells = 6400;                // 장애물 격자: 1m x 1m
	const float MinPos = 2.0f;                  // 외벽 두께 2
	const float MaxPos = WorldSize - 2.0f;      // 6398

	inline int ToSector(float v)
	{
		int s = (int)(v / SectorSize);
		if (s < 0) s = 0;
		if (s >= SectorCount) s = SectorCount - 1;
		return s;
	}
}

////////////////////////////////////////////////////////////////////////
// 장애물 맵 (BMP, 1픽셀 = 1m x 1m, 이미지 좌하단 = 월드 (0,0), 위쪽 = +Z)
//  - 어두운 픽셀(밝기 < 64)  : WALL  → 이동·총알 차단
//  - 중간 밝기(64 ~ 223)     : LOW   → 이동만 차단 (총알 통과)
//  - 밝은 픽셀(>= 224)       : EMPTY
//  1/4/8/24/32bpp 비압축 BMP 지원 (그림판 저장 형식)
////////////////////////////////////////////////////////////////////////
class ObstacleMap
{
public:
	enum : uint8_t { EMPTY = 0, LOW = 1, WALL = 2 };

	bool LoadBmp(const char* path, std::string& err);
	bool Loaded() const { return m_loaded; }

	uint8_t At(int x, int z) const
	{
		if (x < 0 || z < 0 || x >= MapConst::WorldCells || z >= MapConst::WorldCells) return WALL;
		if (!m_loaded) return EMPTY;
		return m_cells[(size_t)z * MapConst::WorldCells + x];
	}
	bool BlocksMove(int x, int z) const { return At(x, z) != EMPTY; }
	bool BlocksBullet(int x, int z) const { return At(x, z) == WALL; }

	// 원(x,z,r)이 이동 차단 셀과 겹치는가
	bool CircleBlocked(float x, float z, float r) const;
	// 선분 A→B가 지나는 셀 중 차단 셀이 있는가 (bullets=true: WALL만, false: WALL+LOW)
	bool SegmentBlocked(float ax, float az, float bx, float bz, bool bullets) const;
	// (x,z) 근처에서 반경 r 원이 비어 있는 위치 탐색 (나선형, 최대 maxDist)
	bool FindFree(float& x, float& z, float r, float maxDist) const;

	uint32_t Hash() const { return m_hash; }
	int WallCells() const { return m_wall; }
	int LowCells() const { return m_low; }
	int ImageWidth() const { return m_imgW; }
	int ImageHeight() const { return m_imgH; }

private:
	void ComputeHash();

	bool m_loaded = false;
	std::vector<uint8_t> m_cells;
	uint32_t m_hash = 0;
	int m_wall = 0, m_low = 0, m_imgW = 0, m_imgH = 0;
};

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
