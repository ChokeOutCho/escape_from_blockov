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

	uint8_t Classify(uint8_t r, uint8_t g, uint8_t b)
	{
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
	m_wall = m_low = 0;
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
		}
	}
	m_imgW = width;
	m_imgH = height;
	m_loaded = true;
	ComputeHash();
	return true;
}

void ObstacleMap::ComputeHash()
{
	// FNV-1a 32bit, 셀 타입 바이트를 z=0..6399, x=0..6399 순서로. 클라 ObstacleMapImporter와 동일해야 한다.
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

bool ObstacleMap::SegmentBlocked(float ax, float az, float bx, float bz, bool bullets) const
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
		bool blocked = bullets ? BlocksBullet(cx, cz) : BlocksMove(cx, cz);
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
