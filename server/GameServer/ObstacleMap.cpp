#include "ObstacleMap.h"
#include <fstream>
#include <iterator>

////////////////////////////////////////////////////////////////////////
// ObstacleMap
////////////////////////////////////////////////////////////////////////
namespace
{
	uint32_t RdU32(const std::vector<uint8_t>& b, size_t o) { return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
	uint16_t RdU16(const std::vector<uint8_t>& b, size_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); }

	// 셀 값: EMPTY 0, LOW 1, WALL 2, DEST 3 + level*16 (level 1~10)
	uint8_t Classify(uint8_t r, uint8_t g, uint8_t b)
	{
		if (r >= 150 && g <= 100 && b <= 100)
		{
			int level = 1 + (r - 150) * 9 / 105;
			return (uint8_t)(ObstacleMap::DEST + level * 16);
		}
		int lum = (299 * r + 587 * g + 114 * b) / 1000;
		if (lum < 64) return ObstacleMap::WALL;
		if (lum < 224) return ObstacleMap::LOW;
		return ObstacleMap::EMPTY;
	}
}

bool ObstacleMap::LoadBmp(const char* path, std::string& err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { err = "file not found"; return false; }
	std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') { err = "not a BMP file"; return false; }

	uint32_t offBits = RdU32(b, 10);
	uint32_t hdrSize = RdU32(b, 14);
	int32_t width = (int32_t)RdU32(b, 18);
	int32_t height = (int32_t)RdU32(b, 22);
	uint16_t bpp = RdU16(b, 28);
	uint32_t compression = RdU32(b, 30);
	uint32_t clrUsed = hdrSize >= 40 ? RdU32(b, 46) : 0;

	bool topDown = height < 0;
	if (topDown) height = -height;
	if (width <= 0 || height <= 0) { err = "invalid size"; return false; }
	if (!(compression == 0 || (compression == 3 && (bpp == 32 || bpp == 16))))
	{
		err = "compressed BMP not supported (save as 24-bit / 16-color / monochrome bitmap)";
		return false;
	}
	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) { err = "unsupported bit depth " + std::to_string(bpp); return false; }

	// 팔레트
	std::vector<uint8_t> palType;
	if (bpp <= 8)
	{
		uint32_t n = clrUsed ? clrUsed : (1u << bpp);
		size_t po = 14 + hdrSize;
		if (po + n * 4 > b.size()) { err = "palette truncated"; return false; }
		for (uint32_t i = 0; i < n; i++)
			palType.push_back(Classify(b[po + i * 4 + 2], b[po + i * 4 + 1], b[po + i * 4 + 0]));
	}

	size_t stride = (((size_t)width * bpp + 31) / 32) * 4;
	if (offBits + stride * height > b.size()) { err = "pixel data truncated"; return false; }

	const int W = MapConst::WorldCells;
	m_cells.assign((size_t)W * W, EMPTY);
	m_wall = m_low = m_dest = 0;
	int useW = width < W ? width : W;
	int useH = height < W ? height : W;
	for (int row = 0; row < height; row++)
	{
		// row: 저장 순서. 이미지 위에서부터의 행 번호 → 월드 z = height-1-rowFromTop
		int rowFromTop = topDown ? row : (height - 1 - row);
		int z = height - 1 - rowFromTop;
		if (z >= useH) continue;
		const uint8_t* p = &b[offBits + stride * row];
		for (int x = 0; x < useW; x++)
		{
			uint8_t t;
			switch (bpp)
			{
			case 1: { int idx = (p[x >> 3] >> (7 - (x & 7))) & 1; t = idx < (int)palType.size() ? palType[idx] : (uint8_t)EMPTY; break; }
			case 4: { int idx = (x & 1) ? (p[x >> 1] & 0x0F) : (p[x >> 1] >> 4); t = idx < (int)palType.size() ? palType[idx] : (uint8_t)EMPTY; break; }
			case 8: { int idx = p[x]; t = idx < (int)palType.size() ? palType[idx] : (uint8_t)EMPTY; break; }
			case 24: t = Classify(p[x * 3 + 2], p[x * 3 + 1], p[x * 3]); break;
			default: t = Classify(p[x * 4 + 2], p[x * 4 + 1], p[x * 4]); break;
			}
			m_cells[(size_t)z * W + x] = t;
			if (t == WALL) m_wall++;
			else if (t == LOW) m_low++;
			else if ((t & 3) == DEST) m_dest++;
		}
	}
	m_imgW = width;
	m_imgH = height;
	m_loaded = true;
	ComputeHash();
	BuildCovers();
	if ((int)m_covers.size() > MAX_COVERS) { err = "too many destructible covers"; m_loaded = false; return false; }
	return true;
}

// 파괴 가능 엄폐물 묶음 만들기: 같은 셀 값으로 상하좌우 이어진 칸 = 1개. z→x 순서로 처음 만나는 묶음부터 id 0,1,2...
void ObstacleMap::BuildCovers()
{
	const int W = MapConst::WorldCells;
	m_coverOf.assign((size_t)W * W, 0);
	m_covers.clear();
	m_coverCells.clear();
	std::vector<uint32_t> stack;
	for (int z = 0; z < W; z++)
		for (int x = 0; x < W; x++)
		{
			size_t i = (size_t)z * W + x;
			uint8_t v = m_cells[i];
			if ((v & 3) != DEST || m_coverOf[i] != 0) continue;
			if ((int)m_covers.size() >= MAX_COVERS) { m_covers.push_back(Cover{}); return; }
			Cover c{};
			c.id = (uint16_t)m_covers.size();
			c.maxHp = (uint8_t)((v >> 4) * 10);
			c.x0 = c.x1 = x; c.z0 = c.z1 = z;
			c.cellStart = (uint32_t)m_coverCells.size();
			uint16_t mark = (uint16_t)(c.id + 1);
			m_coverOf[i] = mark;
			stack.clear();
			stack.push_back((uint32_t)i);
			while (!stack.empty())
			{
				uint32_t cur = stack.back(); stack.pop_back();
				m_coverCells.push_back(cur);
				int cx = (int)(cur % W), cz = (int)(cur / W);
				if (cx < c.x0) c.x0 = cx; if (cx > c.x1) c.x1 = cx;
				if (cz < c.z0) c.z0 = cz; if (cz > c.z1) c.z1 = cz;
				const int nx[4] = { cx + 1, cx - 1, cx, cx }, nz[4] = { cz, cz, cz + 1, cz - 1 };
				for (int k = 0; k < 4; k++)
				{
					if (nx[k] < 0 || nz[k] < 0 || nx[k] >= W || nz[k] >= W) continue;
					size_t j = (size_t)nz[k] * W + nx[k];
					if (m_cells[j] != v || m_coverOf[j] != 0) continue;
					m_coverOf[j] = mark;
					stack.push_back((uint32_t)j);
				}
			}
			c.cellCount = (uint32_t)m_coverCells.size() - c.cellStart;
			c.cx = (c.x0 + c.x1 + 1) * 0.5f;
			c.cz = (c.z0 + c.z1 + 1) * 0.5f;
			c.sx = MapConst::ToSector(c.cx);
			c.sy = MapConst::ToSector(c.cz);
			m_covers.push_back(c);
		}
}

float ObstacleMap::DistanceToCover(int coverId, float x, float z) const
{
	if (coverId < 0 || coverId >= (int)m_covers.size()) return 1e30f;
	const Cover& c = m_covers[coverId];
	const int W = MapConst::WorldCells;
	float best = 1e30f;
	for (uint32_t k = 0; k < c.cellCount; k++)
	{
		uint32_t cell = m_coverCells[c.cellStart + k];
		float x0 = (float)(cell % W), z0 = (float)(cell / W);
		float nx = x < x0 ? x0 : (x > x0 + 1 ? x0 + 1 : x);
		float nz = z < z0 ? z0 : (z > z0 + 1 ? z0 + 1 : z);
		float d = sqrtf((x - nx) * (x - nx) + (z - nz) * (z - nz));
		if (d < best) best = d;
	}
	return best;
}

void ObstacleMap::ComputeHash()
{
	// FNV-1a 32bit, 셀 타입 바이트를 z=0..1499, x=0..1499 순서로. 클라 ObstacleMapImporter와 동일해야 한다.
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < m_cells.size(); i++)
	{
		h ^= m_cells[i];
		h *= 16777619u;
	}
	m_hash = h;
}

bool ObstacleMap::CircleBlocked(float x, float z, float r) const
{
	if (!m_loaded) return false;
	int x0 = (int)floorf(x - r), x1 = (int)floorf(x + r);
	int z0 = (int)floorf(z - r), z1 = (int)floorf(z + r);
	for (int cz = z0; cz <= z1; cz++)
		for (int cx = x0; cx <= x1; cx++)
		{
			if (!BlocksMove(cx, cz)) continue;
			float nx = x < cx ? (float)cx : (x > cx + 1 ? (float)cx + 1 : x);
			float nz = z < cz ? (float)cz : (z > cz + 1 ? (float)cz + 1 : z);
			float dx = x - nx, dz = z - nz;
			if (dx * dx + dz * dz < r * r) return true;
		}
	return false;
}

bool ObstacleMap::SegmentBlocked(float ax, float az, float bx, float bz, bool bullets, const uint8_t* destroyed, int ignoreCover) const
{
	if (!m_loaded) return false;
	int cx = (int)floorf(ax), cz = (int)floorf(az);
	int ex = (int)floorf(bx), ez = (int)floorf(bz);
	float dx = bx - ax, dz = bz - az;
	int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
	int stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
	const float INF = 1e30f;
	float tDeltaX = stepX ? fabsf(1.0f / dx) : INF;
	float tDeltaZ = stepZ ? fabsf(1.0f / dz) : INF;
	float tMaxX = stepX > 0 ? (cx + 1 - ax) / dx : (stepX < 0 ? (ax - cx) / -dx : INF);
	float tMaxZ = stepZ > 0 ? (cz + 1 - az) / dz : (stepZ < 0 ? (az - cz) / -dz : INF);

	for (int guard = 0; guard < 20000; guard++)
	{
		bool blocked;
		if (!bullets) blocked = BlocksMove(cx, cz);
		else
		{
			uint8_t t = At(cx, cz);
			if (t == WALL) blocked = true;
			else if (t == DEST)
			{
				int id = CoverAt(cx, cz);
				blocked = id != ignoreCover && !(destroyed && id >= 0 && destroyed[id]);
			}
			else blocked = false;
		}
		if (blocked) return true;
		if (cx == ex && cz == ez) return false;
		if (tMaxX < tMaxZ) { if (tMaxX > 1.0f) return false; cx += stepX; tMaxX += tDeltaX; }
		else { if (tMaxZ > 1.0f) return false; cz += stepZ; tMaxZ += tDeltaZ; }
	}
	return true;
}

bool ObstacleMap::FindFree(float& x, float& z, float r, float maxDist) const
{
	if (!CircleBlocked(x, z, r)) return true;
	for (float d = 1.0f; d <= maxDist; d += 1.0f)
	{
		int n = (int)(d * 6.2832f / 1.0f) + 8;
		for (int i = 0; i < n; i++)
		{
			float a = 6.2832f * i / n;
			float tx = x + cosf(a) * d, tz = z + sinf(a) * d;
			if (tx < MapConst::MinPos || tz < MapConst::MinPos || tx > MapConst::MaxPos || tz > MapConst::MaxPos) continue;
			if (!CircleBlocked(tx, tz, r)) { x = tx; z = tz; return true; }
		}
	}
	return false;
}
