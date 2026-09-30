#pragma once
////////////////////////////////////////////////////////////////////////
// 섹터 그리드 (game-spec 9.4). 50x50, 셀마다 플레이어 목록.
// BattleContent 전용(단일 스레드).
////////////////////////////////////////////////////////////////////////
#include <vector>
#include <algorithm>
#include <cstdlib>
#include "GameData.h"
#include "GamePlayer.h"

class SectorMap
{
public:
	SectorMap() : m_cells(MapConst::SectorCount * MapConst::SectorCount) {}

	void Add(GamePlayer* p, int sx, int sy)
	{
		p->sectorX = sx;
		p->sectorY = sy;
		m_cells[Index(sx, sy)].push_back(p);
	}

	void Remove(GamePlayer* p)
	{
		if (p->sectorX < 0) return;
		auto& v = m_cells[Index(p->sectorX, p->sectorY)];
		auto it = std::find(v.begin(), v.end(), p);
		if (it != v.end()) { *it = v.back(); v.pop_back(); }
		p->sectorX = p->sectorY = -1;
	}

	const std::vector<GamePlayer*>& Cell(int sx, int sy) const { return m_cells[Index(sx, sy)]; }

	static bool InRange(int sx, int sy) { return sx >= 0 && sy >= 0 && sx < MapConst::SectorCount && sy < MapConst::SectorCount; }

	// (cx,cy) 중심 반경 r 섹터 안의 모든 플레이어에 fn 호출
	template <typename Fn>
	void ForEachInView(int cx, int cy, int r, Fn fn) const
	{
		for (int y = cy - r; y <= cy + r; y++)
			for (int x = cx - r; x <= cx + r; x++)
			{
				if (!InRange(x, y)) continue;
				for (GamePlayer* p : m_cells[Index(x, y)]) fn(p);
			}
	}

	// 두 섹터가 반경 r 안인지 (대칭)
	static bool IsNear(int ax, int ay, int bx, int by, int r)
	{
		return abs(ax - bx) <= r && abs(ay - by) <= r;
	}

	// 반경 r 안 인원 수
	int CountInView(int cx, int cy, int r) const
	{
		int n = 0;
		ForEachInView(cx, cy, r, [&](GamePlayer* p) { if (p->IsAlive()) n++; });
		return n;
	}

private:
	static int Index(int sx, int sy) { return sy * MapConst::SectorCount + sx; }
	std::vector<std::vector<GamePlayer*>> m_cells;
};
