#pragma once
////////////////////////////////////////////////////////////////////////
// 맵 상수 + 장애물 맵 (NetLib 의존 없음)
//  - GameServer와 DummyClient가 함께 컴파일한다.
////////////////////////////////////////////////////////////////////////
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

namespace MapConst
{
	const float SectorSize = 50.0f;
	const int SectorCount = 30;                 // 30 x 30
	const float WorldSize = SectorSize * SectorCount;   // 1500
	const int WorldCells = 1500;                // 장애물 격자: 1m x 1m
	const float MinPos = 2.0f;                  // 외벽 두께 2
	const float MaxPos = WorldSize - 2.0f;      // 1498

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
