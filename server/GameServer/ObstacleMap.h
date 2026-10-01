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
//  - 빨강(R>=150, G<=100, B<=100): DEST → 파괴 가능한 엄폐물 (멀쩡하면 이동·총알 차단, 파괴되면 통과)
//                                  체력 단계 level = 1 + (R-150)*9/105 (1~10), 체력 = level*10
//  - 어두운 픽셀(밝기 < 64)  : WALL  → 이동·총알 차단
//  - 중간 밝기(64 ~ 223)     : LOW   → 이동만 차단 (총알 통과)
//  - 밝은 픽셀(>= 224)       : EMPTY
//  셀 값(해시 대상): EMPTY 0, LOW 1, WALL 2, DEST 3 + level*16
//  파괴 가능 엄폐물 1개 = 같은 셀 값으로 상하좌우 이어진 칸 묶음. id는 z→x 순서로 처음 만나는 묶음부터 0,1,2...
//  (클라 ObstacleMapImporter와 같은 규칙·같은 해시)
//  1/4/8/24/32bpp 비압축 BMP 지원 (그림판 저장 형식)
////////////////////////////////////////////////////////////////////////
class ObstacleMap
{
public:
	enum : uint8_t { EMPTY = 0, LOW = 1, WALL = 2, DEST = 3 };

	struct Cover
	{
		uint16_t id;
		uint8_t maxHp;
		int x0, z0, x1, z1;         // 칸 범위 (포함)
		float cx, cz;               // 중심
		int sx, sy;                 // 중심 섹터
		uint32_t cellStart, cellCount;   // CoverCells() 안의 범위 (z*1500+x)
	};
	static const int MAX_COVERS = 65000;

	bool LoadBmp(const char* path, std::string& err);
	bool Loaded() const { return m_loaded; }

	uint8_t At(int x, int z) const
	{
		if (x < 0 || z < 0 || x >= MapConst::WorldCells || z >= MapConst::WorldCells) return WALL;
		if (!m_loaded) return EMPTY;
		return (uint8_t)(m_cells[(size_t)z * MapConst::WorldCells + x] & 3);
	}
	bool BlocksMove(int x, int z) const { return At(x, z) != EMPTY; }
	// 방의 파괴 상태(엄폐물 id별)를 반영한 이동 차단. destroyed가 없으면 BlocksMove와 같다
	bool BlocksMove(int x, int z, const uint8_t* destroyed) const
	{
		uint8_t t = At(x, z);
		if (t == EMPTY) return false;
		if (t == DEST && destroyed)
		{
			int id = CoverAt(x, z);
			return !(id >= 0 && destroyed[id]);
		}
		return true;
	}
	// 파괴 상태를 모를 때(정적): 파괴 가능 엄폐물도 총알을 막는 것으로 본다
	bool BlocksBullet(int x, int z) const { uint8_t t = At(x, z); return t == WALL || t == DEST; }
	// 파괴 가능 엄폐물 id (없으면 -1)
	int CoverAt(int x, int z) const
	{
		if (!m_loaded || x < 0 || z < 0 || x >= MapConst::WorldCells || z >= MapConst::WorldCells) return -1;
		return (int)m_coverOf[(size_t)z * MapConst::WorldCells + x] - 1;
	}
	const std::vector<Cover>& Covers() const { return m_covers; }
	const std::vector<uint32_t>& CoverCells() const { return m_coverCells; }
	// 점(x,z)에서 엄폐물 칸까지 최단 거리
	float DistanceToCover(int coverId, float x, float z) const;

	// 원(x,z,r)이 이동 차단 셀과 겹치는가 (destroyed: 파괴된 엄폐물 칸은 빈 칸으로 본다)
	bool CircleBlocked(float x, float z, float r, const uint8_t* destroyed = nullptr) const;
	// 선분 A→B가 지나는 셀 중 차단 셀이 있는가
	//  bullets=false: WALL+LOW+멀쩡한 DEST(이동). destroyed가 없으면 DEST는 항상 막음
	//  bullets=true : WALL + 멀쩡한 DEST. destroyed(엄폐물 id별 파괴 여부)가 없으면 DEST는 항상 막음. ignoreCover는 막지 않음
	bool SegmentBlocked(float ax, float az, float bx, float bz, bool bullets,
	                    const uint8_t* destroyed = nullptr, int ignoreCover = -1) const;
	// (x,z) 근처에서 반경 r 원이 비어 있는 위치 탐색 (나선형, 최대 maxDist)
	bool FindFree(float& x, float& z, float r, float maxDist) const;

	uint32_t Hash() const { return m_hash; }
	int WallCells() const { return m_wall; }
	int LowCells() const { return m_low; }
	int DestCells() const { return m_dest; }
	int ImageWidth() const { return m_imgW; }
	int ImageHeight() const { return m_imgH; }

private:
	void ComputeHash();
	void BuildCovers();

	bool m_loaded = false;
	std::vector<uint8_t> m_cells;           // 셀 값 (하위 2비트 = 종류, DEST는 상위 4비트 = level)
	std::vector<uint16_t> m_coverOf;        // 엄폐물 id + 1 (0 = 없음)
	std::vector<Cover> m_covers;
	std::vector<uint32_t> m_coverCells;
	uint32_t m_hash = 0;
	int m_wall = 0, m_low = 0, m_dest = 0, m_imgW = 0, m_imgH = 0;
};
