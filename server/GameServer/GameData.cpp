#include "GameData.h"
#include "NetLibBridge.h"
#include <fstream>
#include <sstream>
#include <iterator>
#include <cmath>

bool WeaponTable::Load(const char* path, float maxRange)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	while (std::getline(f, line))
	{
		size_t hash = line.find('#');
		if (hash != std::string::npos) line = line.substr(0, hash);
		std::istringstream is(line);
		int id, damage, interval, magazine, reload, pellets, pierce;
		WeaponDef d;
		if (!(is >> id >> d.name >> damage >> d.range >> d.projectileSpeed >> interval >> d.projectileRadius
			>> magazine >> reload >> d.spreadDeg >> pellets >> pierce))
			continue;
		if (id < 1 || id > 255) { GameLog("[weapons] invalid id %d, skipped", id); continue; }
		d.id = (uint8_t)id;
		d.damage = (uint16_t)damage;
		d.fireIntervalMs = (uint16_t)(interval < 1 ? 1 : interval);
		d.magazineSize = (uint16_t)magazine;
		d.reloadMs = (uint16_t)reload;
		d.pellets = (uint8_t)(pellets < 1 ? 1 : (pellets > 8 ? 8 : pellets));
		d.pierce = (uint8_t)(pierce < 0 ? 0 : (pierce > 3 ? 3 : pierce));
		if (d.projectileSpeed <= 0) d.projectileSpeed = 1;
		// 사거리는 시야 보장 거리 이하 (game-spec 3.1)
		if (d.range > maxRange)
		{
			GameLog("[weapons] weapon %d range %.1f > %.1f (view guarantee), clamped", id, d.range, maxRange);
			d.range = maxRange;
		}
		m_defs.push_back(d);
	}
	return !m_defs.empty();
}

const WeaponDef* WeaponTable::Find(uint8_t id) const
{
	for (const auto& d : m_defs)
		if (d.id == id) return &d;
	return nullptr;
}

bool SpawnTable::Load(const char* path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	while (std::getline(f, line))
	{
		size_t hash = line.find('#');
		if (hash != std::string::npos) line = line.substr(0, hash);
		std::istringstream is(line);
		int sx, sy;
		if (!(is >> sx >> sy)) continue;
		if (sx < 0 || sy < 0 || sx >= MapConst::SectorCount || sy >= MapConst::SectorCount) continue;
		SpawnPoint p{ sx, sy, (sx + 0.5f) * MapConst::SectorSize, (sy + 0.5f) * MapConst::SectorSize };
		m_spawns.push_back(p);
	}
	return !m_spawns.empty();
}
