#include "BattleContent.h"
#include <cmath>
#include <algorithm>

namespace
{
	const float PI = 3.14159265358979f;
	const int MOVE_VIOLATION_WINDOW_MS = 5000;
	const int MOVE_VIOLATION_LIMIT = 10;
	const int CHEAT_WINDOW_MS = 10000;
	const int CHEAT_LIMIT = 20;
	const int HISTORY_REFILL_MS = 200;
	const float FIRE_TOKEN_CAP = 3.0f;
	const float FIRE_ORIGIN_TOLERANCE = 3.0f;
	const int FUTURE_VIEWTIME_TOLERANCE_MS = 50;

	bool Finite(float v) { return std::isfinite(v); }
	float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	float NormalizeAngle(float deg)
	{
		float a = fmodf(deg, 360.0f);
		if (a < 0) a += 360.0f;
		return a;
	}
}

BattleContent::BattleContent(int roomNo, const GameConfig& cfg, const WeaponTable& weapons, const SpawnTable& spawns, const ObstacleMap& obstacles)
	: NetLib_Content(cfg.battleTickMs), m_roomNo(roomNo), m_cfg(cfg), m_weapons(weapons), m_spawns(spawns), m_obstacles(obstacles),
	  m_rng((unsigned)(roomNo * 7919 + timeGetTime()))
{
}

BattleContent::~BattleContent()
{
	for (auto& kv : m_players) delete kv.second;
}

bool BattleContent::TryReserve()
{
	int prev = m_reserved.fetch_add(1);
	if (prev >= m_cfg.roomCapacity)
	{
		m_reserved.fetch_sub(1);
		return false;
	}
	return true;
}

void BattleContent::OnBegin()
{
	m_lastUpdateTime = GetServerTimeMs();
	m_lastSecondTick = m_lastUpdateTime;
	GameLog("[room %d] begin (tick %dms, capacity %d)", m_roomNo, m_cfg.battleTickMs, m_cfg.roomCapacity);
}

////////////////////////////////////////////////////////////////////////
// 입장
////////////////////////////////////////////////////////////////////////
void BattleContent::OnEnter(unsigned long long sessionHandle, void* completionKey)
{
	GamePlayer* p = (GamePlayer*)completionKey;
	uint32_t now = GetServerTimeMs();
	if (p == nullptr || p->sessionHandle != sessionHandle)
	{
		// 비정상. 예약은 EntryContent가 잡아둔 것이므로 여기서 해제
		m_reserved.fetch_sub(1);
		m_server->Disconnect(sessionHandle);
		return;
	}

	p->playerId = m_nextPlayerId++;
	if (m_nextPlayerId == 0) m_nextPlayerId = 1;
	p->state = PlayerState::Alive;
	p->maxHp = (uint16_t)m_cfg.maxHp;
	p->hp = p->maxHp;
	p->weaponId = (uint8_t)m_cfg.defaultWeaponId;
	p->enterTime = now;
	p->lastRecv = now;
	p->scoreReachedTick = now;
	p->moveBudget = m_cfg.moveSpeed;    // 시작 시 1초치
	p->fireTokens = FIRE_TOKEN_CAP;
	ChooseSpawn(p->x, p->z);
	p->vx = p->vz = 0;
	p->aim = 0;
	p->RecordHistory(now);

	m_players[sessionHandle] = p;
	m_byId[p->playerId] = p;
	m_playerCount.store((int)m_players.size());
	m_sectors.Add(p, MapConst::ToSector(p->x), MapConst::ToSector(p->z));

	SendEnterSequence(p);

	// 주변에 신규 1명 알림
	std::vector<GamePlayer*> view;
	CollectView(p, p, view);
	if (!view.empty())
	{
		std::vector<unsigned long long> hs;
		Handles(view, hs);
		Packet* pkt = BuildCreateOne(p);
		Multicast(hs, pkt);
	}
}

void BattleContent::SendEnterSequence(GamePlayer* p)
{
	// 1) PT_SC_ENTER_GAME
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_ENTER_GAME);
	W8(pkt, ENTER_OK);
	W32(pkt, p->playerId);
	W8(pkt, (uint8_t)m_roomNo);
	WF(pkt, p->x); WF(pkt, p->z);
	W16(pkt, p->hp); W16(pkt, p->maxHp);
	WF(pkt, m_cfg.moveSpeed);
	WF(pkt, m_cfg.characterRadius);
	W8(pkt, p->weaponId);
	W32(pkt, GetServerTimeMs());
	WName(pkt, p->name, NAME_LEN);
	W32(pkt, m_obstacles.Hash());
	WF(pkt, m_cfg.sprintMultiplier);
	SendTo(p, pkt);

	// 2) PT_SC_WEAPON_DEFS (16개 단위)
	const auto& defs = m_weapons.All();
	for (size_t start = 0; start < defs.size(); start += MAX_WEAPON_DEFS_PER_PACKET)
	{
		size_t n = std::min(defs.size() - start, (size_t)MAX_WEAPON_DEFS_PER_PACKET);
		Packet* w = Packet::NetAlloc();
		W16(w, PT_SC_WEAPON_DEFS);
		W8(w, (uint8_t)n);
		for (size_t i = start; i < start + n; i++)
		{
			const WeaponDef& d = defs[i];
			W8(w, d.id); W16(w, d.damage); WF(w, d.range); WF(w, d.projectileSpeed);
			W16(w, d.fireIntervalMs); WF(w, d.projectileRadius); W16(w, d.magazineSize);
			W16(w, d.reloadMs); WF(w, d.spreadDeg); W8(w, d.pellets); W8(w, d.pierce);
			W8(w, 0); W8(w, 0); W8(w, 0);
		}
		SendTo(p, w);
	}

	// 3) PT_SC_CREATE_CHARACTERS (시야 내 기존 플레이어)
	std::vector<GamePlayer*> view;
	CollectView(p, p, view);
	SendCreateList(p, view);

	// 4) PT_SC_RANKING_TOP3 (현재 상태)
	std::vector<RankEntry> top;
	ComputeTop3(top);
	Packet* rk = BuildRanking(top);
	std::vector<unsigned long long> me{ p->sessionHandle };
	Multicast(me, rk);
}

void BattleContent::ChooseSpawn(float& outX, float& outZ)
{
	// 테스트 모드: 지정 섹터(기본 0,0) 안 무작위 위치
	if (m_cfg.testMode)
	{
		const float S = (float)MapConst::SectorSize;
		int sx = std::clamp(m_cfg.testSpawnSectorX, 0, MapConst::SectorCount - 1);
		int sy = std::clamp(m_cfg.testSpawnSectorY, 0, MapConst::SectorCount - 1);
		const float margin = m_cfg.characterRadius + 4.0f;
		std::uniform_real_distribution<float> ux(sx * S + margin, (sx + 1) * S - margin), uz(sy * S + margin, (sy + 1) * S - margin);
		outX = Clampf(ux(m_rng), MapConst::MinPos, MapConst::MaxPos);
		outZ = Clampf(uz(m_rng), MapConst::MinPos, MapConst::MaxPos);
		if (!m_obstacles.FindFree(outX, outZ, m_cfg.characterRadius + 0.5f, 48.0f))
			GameLog("[room %d] no free test spawn position near (%.1f, %.1f)", m_roomNo, outX, outZ);
		return;
	}

	const auto& sp = m_spawns.All();
	if (sp.empty())
	{
		outX = outZ = MapConst::WorldSize * 0.5f;
		return;
	}
	const int r = m_cfg.viewSectorRadius;
	std::vector<int> empty;
	int best = -1, bestCount = 1 << 30;
	for (int i = 0; i < (int)sp.size(); i++)
	{
		int c = m_sectors.CountInView(sp[i].sx, sp[i].sy, r);
		if (c == 0) empty.push_back(i);
		if (c < bestCount) { bestCount = c; best = i; }
	}
	int idx = empty.empty() ? best : empty[std::uniform_int_distribution<int>(0, (int)empty.size() - 1)(m_rng)];
	outX = sp[idx].x;
	outZ = sp[idx].z;

	// 같은 섹터에 이미 사람이 있으면 반경 32 내 무작위 오프셋
	if (!m_sectors.Cell(sp[idx].sx, sp[idx].sy).empty() && m_cfg.spawnOffsetRadius > 4)
	{
		std::uniform_real_distribution<float> ang(0, 2 * PI), rad(4.0f, (float)m_cfg.spawnOffsetRadius);
		float a = ang(m_rng), d = rad(m_rng);
		outX += cosf(a) * d;
		outZ += sinf(a) * d;
	}
	outX = Clampf(outX, MapConst::MinPos, MapConst::MaxPos);
	outZ = Clampf(outZ, MapConst::MinPos, MapConst::MaxPos);
	// 엄폐물 위라면 가까운 빈 곳으로
	if (!m_obstacles.FindFree(outX, outZ, m_cfg.characterRadius + 0.5f, 48.0f))
		GameLog("[room %d] no free spawn position near (%.1f, %.1f)", m_roomNo, outX, outZ);
}

////////////////////////////////////////////////////////////////////////
// 수신
////////////////////////////////////////////////////////////////////////
void BattleContent::OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen)
{
	auto it = m_players.find(sessionHandle);
	if (it == m_players.end()) return;
	GamePlayer* p = it->second;
	uint32_t now = GetServerTimeMs();
	p->lastRecv = now;

	if (payloadLen < 2) { Kick(p, KICK_INVALID_PACKET, now); return; }
	PayloadReader r(payload, payloadLen);
	uint16_t type = r.U16();

	switch (type)
	{
	case PT_CS_PING:
	{
		if (payloadLen != LEN_CS_PING) { Kick(p, KICK_INVALID_PACKET, now); return; }
		uint32_t clientTime = r.U32();
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_PONG);
		W32(pkt, clientTime);
		W32(pkt, GetServerTimeMs());
		SendTo(p, pkt);
		return;
	}
	case PT_CS_HEARTBEAT:
		if (payloadLen != LEN_CS_HEARTBEAT) Kick(p, KICK_INVALID_PACKET, now);
		return;
	default:
		break;
	}

	// 이하 게임 입력: 사망/끊기 예약 상태면 무시
	if (!p->IsAlive() || p->disconnecting) return;

	switch (type)
	{
	case PT_CS_MOVE:
		if (payloadLen != LEN_CS_MOVE) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleMove(p, r, now);
		break;
	case PT_CS_FIRE:
		if (payloadLen != LEN_CS_FIRE) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleFire(p, r, now);
		break;
	case PT_CS_HIT_REPORT:
	{
		if (payloadLen < LEN_CS_HIT_REPORT_HEAD) { Kick(p, KICK_INVALID_PACKET, now); return; }
		int count = r.U8();
		if (count < 1 || count > MAX_HIT_ITEMS || payloadLen != LEN_CS_HIT_REPORT_HEAD + LEN_HIT_ITEM * count)
		{
			Kick(p, KICK_INVALID_PACKET, now);
			return;
		}
		HandleHitReport(p, r, count, now);
		break;
	}
	default:
		Kick(p, KICK_INVALID_PACKET, now);
		break;
	}
}

void BattleContent::HandleMove(GamePlayer* p, PayloadReader& r, uint32_t now)
{
	float px = r.F(), pz = r.F(), vx = r.F(), vz = r.F(), aim = r.F();
	uint16_t seq = r.U16();

	bool valid = Finite(px) && Finite(pz) && Finite(vx) && Finite(vz) && Finite(aim);
	float tx = px, tz = pz;
	bool clamped = false;
	if (valid)
	{
		tx = Clampf(px, MapConst::MinPos, MapConst::MaxPos);
		tz = Clampf(pz, MapConst::MinPos, MapConst::MaxPos);
		clamped = (tx != px || tz != pz);

		float speed = sqrtf(vx * vx + vz * vz);
		if (speed > m_cfg.moveSpeed * m_cfg.sprintMultiplier * 1.1f) valid = false;   // 달리기 포함
	}
	// 엄폐물: 이동 경로(중심선)와 도착 위치(원)가 막힌 셀과 겹치면 거부
	if (valid && (m_obstacles.SegmentBlocked(p->x, p->z, tx, tz, false) ||
	              m_obstacles.CircleBlocked(tx, tz, m_cfg.characterRadius - 0.1f)))
		valid = false;
	if (valid)
	{
		float dx = tx - p->x, dz = tz - p->z;
		float d = sqrtf(dx * dx + dz * dz);
		if (d > p->moveBudget + 0.5f) valid = false;
		else p->moveBudget = std::max(0.0f, p->moveBudget - d);
	}

	if (!valid)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_POSITION_CORRECT);
		WF(pkt, p->x); WF(pkt, p->z);
		W16(pkt, seq);
		SendTo(p, pkt);
		if (p->moveViolations.Add(now, MOVE_VIOLATION_WINDOW_MS, MOVE_VIOLATION_LIMIT))
			Kick(p, KICK_CHEAT_SUSPECT, now);
		return;
	}

	p->x = tx; p->z = tz;
	p->vx = clamped ? 0 : vx;
	p->vz = clamped ? 0 : vz;
	p->aim = NormalizeAngle(aim);
	p->RecordHistory(now);

	int nsx = MapConst::ToSector(p->x), nsy = MapConst::ToSector(p->z);
	if (nsx != p->sectorX || nsy != p->sectorY)
		ChangeSector(p, nsx, nsy);

	std::vector<GamePlayer*> view;
	CollectView(p, p, view);
	if (!view.empty())
	{
		std::vector<unsigned long long> hs;
		Handles(view, hs);
		Packet* pkt = Packet::Alloc();
		W16(pkt, PT_SC_MOVE);
		W32(pkt, p->playerId);
		WF(pkt, p->x); WF(pkt, p->z); WF(pkt, p->vx); WF(pkt, p->vz); WF(pkt, p->aim);
		Multicast(hs, pkt);
	}

	if (clamped)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_POSITION_CORRECT);
		WF(pkt, p->x); WF(pkt, p->z);
		W16(pkt, seq);
		SendTo(p, pkt);
	}
}

void BattleContent::HandleFire(GamePlayer* p, PayloadReader& r, uint32_t now)
{
	uint32_t shotSeq = r.U32();
	uint8_t weaponId = r.U8();
	float ox = r.F(), oz = r.F(), dx = r.F(), dz = r.F();
	uint32_t viewTime = r.U32();
	r.U16();    // reserved

	const WeaponDef* w = m_weapons.Find(weaponId);
	bool ok = (w != nullptr && weaponId == p->weaponId);
	ok = ok && (shotSeq > p->lastShotSeq);
	ok = ok && Finite(ox) && Finite(oz) && Finite(dx) && Finite(dz);
	float len = ok ? sqrtf(dx * dx + dz * dz) : 0;
	ok = ok && len >= 0.9f && len <= 1.1f;
	if (ok)
	{
		float ex = ox - p->x, ez = oz - p->z;
		ok = sqrtf(ex * ex + ez * ez) <= FIRE_ORIGIN_TOLERANCE;
	}
	int32_t rewind = TimeDiff(now, viewTime);
	ok = ok && rewind >= -FUTURE_VIEWTIME_TOLERANCE_MS;
	ok = ok && p->fireTokens >= 1.0f;

	if (!ok)
	{
		CountCheat(p, now);
		return;
	}

	if (rewind > m_cfg.maxRewindMs) viewTime = now - (uint32_t)m_cfg.maxRewindMs;
	if (rewind < 0) viewTime = now;
	p->fireTokens -= 1.0f;
	p->lastShotSeq = shotSeq;
	dx /= len; dz /= len;

	ShotRecord& s = p->shots[shotSeq % GamePlayer::SHOT_RING];
	memset(&s, 0, sizeof(s));
	s.valid = true;
	s.seq = shotSeq;
	s.weaponId = weaponId;
	s.ox = ox; s.oz = oz; s.dx = dx; s.dz = dz;
	s.viewTime = viewTime;
	s.recvTime = now;

	std::vector<GamePlayer*> view;
	CollectView(p, p, view);
	if (!view.empty())
	{
		std::vector<unsigned long long> hs;
		Handles(view, hs);
		Packet* pkt = Packet::Alloc();
		W16(pkt, PT_SC_FIRE);
		W32(pkt, p->playerId);
		W32(pkt, shotSeq);
		W8(pkt, weaponId);
		WF(pkt, ox); WF(pkt, oz); WF(pkt, dx); WF(pkt, dz);
		W8(pkt, (uint8_t)(m_rng() & 0xFF));
		Multicast(hs, pkt);
	}
}

void BattleContent::HandleHitReport(GamePlayer* p, PayloadReader& r, int count, uint32_t now)
{
	for (int i = 0; i < count; i++)
	{
		uint32_t shotSeq = r.U32();
		uint8_t pellet = r.U8();
		uint32_t targetId = r.U32();
		float hx = r.F(), hz = r.F();

		if (!p->IsAlive() || p->disconnecting) return;

		GamePlayer* target = nullptr;
		const WeaponDef* w = nullptr;
		if (!ValidateHit(p, shotSeq, pellet, targetId, hx, hz, now, &target, &w))
		{
			CountCheat(p, now);
			if (p->disconnecting) return;
			continue;
		}
		ShotRecord* s = p->FindShot(shotSeq);
		s->hitTargets[pellet][s->hitCount[pellet]++] = targetId;
		ApplyDamage(p, target, shotSeq, w->damage, now);
	}
}

bool BattleContent::ValidateHit(GamePlayer* shooter, uint32_t shotSeq, uint8_t pellet, uint32_t targetId,
                                float hx, float hz, uint32_t now, GamePlayer** outTarget, const WeaponDef** outWeapon)
{
	// 1. 사격 기록 존재
	ShotRecord* s = shooter->FindShot(shotSeq);
	if (s == nullptr) return false;
	const WeaponDef* w = m_weapons.Find(s->weaponId);
	if (w == nullptr) return false;

	// 2. 펠릿 번호
	if (pellet >= w->pellets || pellet >= ShotRecord::MAX_PELLETS) return false;

	// 3. 관통 수 / 중복 대상
	int maxTargets = std::min(1 + (int)w->pierce, ShotRecord::MAX_TARGETS);
	if (s->hitCount[pellet] >= maxTargets) return false;
	for (int i = 0; i < s->hitCount[pellet]; i++)
		if (s->hitTargets[pellet][i] == targetId) return false;

	// 4. 대상 상태 / 시야
	GamePlayer* t = FindById(targetId);
	if (t == nullptr || !t->IsAlive() || t == shooter) return false;
	if (!SectorMap::IsNear(shooter->sectorX, shooter->sectorY, t->sectorX, t->sectorY, m_cfg.viewSectorRadius)) return false;

	// 5. 보고 시한
	float flightMs = w->range / w->projectileSpeed * 1000.0f;
	if ((float)TimeDiff(now, s->recvTime) > flightMs + m_cfg.maxRewindMs + 200.0f) return false;

	// 6. 거리
	if (!Finite(hx) || !Finite(hz)) return false;
	float rx = hx - s->ox, rz = hz - s->oz;
	float dist = sqrtf(rx * rx + rz * rz);
	if (dist > w->range + w->projectileRadius) return false;

	// 7. 각도
	if (dist >= 2.0f)
	{
		float cosA = (rx * s->dx + rz * s->dz) / dist;
		cosA = Clampf(cosA, -1.0f, 1.0f);
		float angleDeg = acosf(cosA) * 180.0f / PI;
		if (angleDeg > w->spreadDeg * 0.5f + 3.0f) return false;
	}

	// 8. 과거 위치 일치
	uint32_t tHit = s->viewTime + (uint32_t)(dist / w->projectileSpeed * 1000.0f);
	float px, pz;
	if (!t->history.PosAt(tHit, px, pz)) return false;
	float ex = hx - px, ez = hz - pz;
	if (sqrtf(ex * ex + ez * ez) > m_cfg.characterRadius + w->projectileRadius + m_cfg.hitTolerance) return false;

	// 9. 사선: 발사 위치 → 충돌 지점 사이에 벽(WALL)이 없어야 한다 (낮은 엄폐물은 통과)
	if (m_obstacles.SegmentBlocked(s->ox, s->oz, hx, hz, true)) return false;

	*outTarget = t;
	*outWeapon = w;
	return true;
}

////////////////////////////////////////////////////////////////////////
// 규칙
////////////////////////////////////////////////////////////////////////
void BattleContent::ApplyDamage(GamePlayer* shooter, GamePlayer* victim, uint32_t shotSeq, uint16_t damage, uint32_t now)
{
	victim->hp = (victim->hp > damage) ? (uint16_t)(victim->hp - damage) : 0;

	std::vector<GamePlayer*> view;
	CollectView(victim, nullptr, view);     // 피해자 3x3 (피해자·사수 포함)
	std::vector<unsigned long long> hs;
	Handles(view, hs);
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_DAMAGE);
	W32(pkt, shooter->playerId);
	W32(pkt, victim->playerId);
	W32(pkt, shotSeq);
	W16(pkt, damage);
	W16(pkt, victim->hp);
	Multicast(hs, pkt);

	if (victim->hp == 0)
		Kill(victim, shooter, now);
}

void BattleContent::Kill(GamePlayer* victim, GamePlayer* killer, uint32_t now)
{
	// 1~2. 피해자 3x3에 사망 알림 후 섹터에서 제거
	std::vector<GamePlayer*> view;
	CollectView(victim, nullptr, view);
	std::vector<unsigned long long> hs;
	Handles(view, hs);
	Packet* die = Packet::Alloc();
	W16(die, PT_SC_PLAYER_DIE);
	W32(die, victim->playerId);
	W32(die, killer ? killer->playerId : 0);
	Multicast(hs, die);

	m_sectors.Remove(victim);
	victim->state = PlayerState::Dead;
	victim->deadTime = now;
	victim->vx = victim->vz = 0;

	// 3. 킬러 점수
	if (killer)
	{
		killer->score += 1 + victim->score / 2;
		killer->kills += 1;
		killer->scoreReachedTick = now;
		Packet* sc = Packet::NetAlloc();
		W16(sc, PT_SC_SCORE);
		W32(sc, killer->score);
		W16(sc, (uint16_t)std::min<uint32_t>(killer->kills, 0xFFFF));
		SendTo(killer, sc);
	}

	// 4. 피해자 결과 → 3초 뒤 끊기
	Packet* res = Packet::NetAlloc();
	W16(res, PT_SC_DEATH_RESULT);
	char16_t emptyName[NAME_LEN] = {};
	WName(res, killer ? killer->name : emptyName, NAME_LEN);
	W32(res, victim->score);
	W32(res, victim->kills);
	W32(res, (uint32_t)TimeDiff(now, victim->enterTime) / 1000);
	SendTo(victim, res);

	ScheduleDisconnect(victim, now + (uint32_t)m_cfg.deathDisconnectMs);
}

void BattleContent::Kick(GamePlayer* p, uint8_t reason, uint32_t now)
{
	if (p->disconnecting) return;
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_KICK);
	W8(pkt, reason);
	SendTo(p, pkt);
	GameLog("[room %d] kick player %u reason %d", m_roomNo, p->playerId, reason);
	ScheduleDisconnect(p, now + 300);   // 킥 패킷이 나갈 시간을 준다
}

void BattleContent::ScheduleDisconnect(GamePlayer* p, uint32_t at)
{
	if (p->disconnecting) return;
	p->disconnecting = true;
	m_pending.push_back({ p->sessionHandle, at });
}

void BattleContent::CountCheat(GamePlayer* p, uint32_t now)
{
	if (p->cheatViolations.Add(now, CHEAT_WINDOW_MS, CHEAT_LIMIT))
		Kick(p, KICK_CHEAT_SUSPECT, now);
}

void BattleContent::ChangeSector(GamePlayer* p, int nsx, int nsy)
{
	const int r = m_cfg.viewSectorRadius;
	int osx = p->sectorX, osy = p->sectorY;

	std::vector<GamePlayer*> removed, added;
	m_sectors.ForEachInView(osx, osy, r, [&](GamePlayer* q) {
		if (q != p && !SectorMap::IsNear(q->sectorX, q->sectorY, nsx, nsy, r)) removed.push_back(q);
	});
	m_sectors.ForEachInView(nsx, nsy, r, [&](GamePlayer* q) {
		if (q != p && !SectorMap::IsNear(q->sectorX, q->sectorY, osx, osy, r)) added.push_back(q);
	});

	m_sectors.Remove(p);
	m_sectors.Add(p, nsx, nsy);

	if (!removed.empty())
	{
		SendDeleteList(p, removed);
		std::vector<unsigned long long> hs;
		Handles(removed, hs);
		Multicast(hs, BuildDeleteOne(p));
	}
	if (!added.empty())
	{
		SendCreateList(p, added);
		std::vector<unsigned long long> hs;
		Handles(added, hs);
		Multicast(hs, BuildCreateOne(p));
	}
}

void BattleContent::RemoveFromWorld(GamePlayer* p)
{
	if (p->sectorX >= 0)
	{
		std::vector<GamePlayer*> view;
		CollectView(p, p, view);
		m_sectors.Remove(p);
		if (!view.empty())
		{
			std::vector<unsigned long long> hs;
			Handles(view, hs);
			Multicast(hs, BuildDeleteOne(p));
		}
	}
}

////////////////////////////////////////////////////////////////////////
// 틱
////////////////////////////////////////////////////////////////////////
void BattleContent::OnUpdate(float deltaTime)
{
	uint32_t now = GetServerTimeMs();
	float dt = deltaTime;

	for (auto& kv : m_players)
	{
		GamePlayer* p = kv.second;
		if (!p->IsAlive()) continue;

		// 이동 예산 (10.1)
		const float maxSpeed = m_cfg.moveSpeed * m_cfg.sprintMultiplier;   // 달리기 최고 속도 기준
		p->moveBudget = std::min(p->moveBudget + maxSpeed * dt * 1.2f, maxSpeed * 1.0f);

		// 사격 토큰 (10.2)
		const WeaponDef* w = m_weapons.Find(p->weaponId);
		if (w)
			p->fireTokens = std::min(FIRE_TOKEN_CAP, p->fireTokens + dt * (1000.0f / w->fireIntervalMs) * 1.1f);

		// 정지 중 이력 보충 (9.5)
		if (TimeDiff(now, p->history.LastTime()) > HISTORY_REFILL_MS)
			p->RecordHistory(now);
	}

	// 1초마다 하트비트 타임아웃
	if (TimeDiff(now, m_lastSecondTick) >= 1000)
	{
		m_lastSecondTick = now;
		for (auto& kv : m_players)
		{
			GamePlayer* p = kv.second;
			if (!p->disconnecting && TimeDiff(now, p->lastRecv) > m_cfg.heartbeatTimeoutMs)
				Kick(p, KICK_TIMEOUT, now);
		}
	}

	// 예약된 끊기
	for (size_t i = 0; i < m_pending.size();)
	{
		if (TimeDiff(now, m_pending[i].at) >= 0)
		{
			m_server->Disconnect(m_pending[i].handle);
			m_pending[i] = m_pending.back();
			m_pending.pop_back();
		}
		else i++;
	}

	UpdateRanking(false);
}

void BattleContent::ComputeTop3(std::vector<RankEntry>& out) const
{
	out.clear();
	std::vector<GamePlayer*> alive;
	alive.reserve(m_players.size());
	for (auto& kv : m_players)
		if (kv.second->IsAlive()) alive.push_back(kv.second);

	auto better = [](GamePlayer* a, GamePlayer* b) {
		if (a->score != b->score) return a->score > b->score;
		int32_t d = (int32_t)(a->scoreReachedTick - b->scoreReachedTick);
		if (d != 0) return d < 0;
		return a->playerId < b->playerId;
	};
	size_t n = std::min<size_t>(3, alive.size());
	std::partial_sort(alive.begin(), alive.begin() + n, alive.end(), better);
	for (size_t i = 0; i < n; i++) out.push_back({ alive[i]->playerId, alive[i]->score });
}

void BattleContent::UpdateRanking(bool force)
{
	std::vector<RankEntry> top;
	ComputeTop3(top);
	bool changed = force || top.size() != m_lastTop.size();
	for (size_t i = 0; !changed && i < top.size(); i++)
		changed = top[i].id != m_lastTop[i].id || top[i].score != m_lastTop[i].score;
	if (!changed) return;
	m_lastTop = top;

	std::vector<unsigned long long> hs;
	for (auto& kv : m_players)
		if (!kv.second->disconnecting || kv.second->IsAlive()) hs.push_back(kv.first);
	Multicast(hs, BuildRanking(top));
}

////////////////////////////////////////////////////////////////////////
// 퇴장
////////////////////////////////////////////////////////////////////////
void BattleContent::OnLeave(unsigned long long sessionHandle)
{
	// 이 게임에서는 방 → 다른 Content 이동이 없다. 만약 발생하면 퇴장과 동일하게 정리.
	OnRelease(sessionHandle, SESSION_LEAVE_CODE::NONE, 0, 0);
}

void BattleContent::OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
{
	auto it = m_players.find(sessionHandle);
	if (it == m_players.end()) return;
	GamePlayer* p = it->second;

	RemoveFromWorld(p);
	m_players.erase(it);
	m_byId.erase(p->playerId);
	m_playerCount.store((int)m_players.size());
	m_reserved.fetch_sub(1);
	GameLog("[room %d] player %u left (code %d, score %u)", m_roomNo, p->playerId, (int)code, p->score);
	delete p;
}

////////////////////////////////////////////////////////////////////////
// 송신 헬퍼
////////////////////////////////////////////////////////////////////////
void BattleContent::SendTo(GamePlayer* p, Packet* netPacket)
{
	m_server->SendPacketFastWithoutIOCount(p->sessionHandle, netPacket);
}

void BattleContent::Multicast(const std::vector<unsigned long long>& handles, Packet* packet)
{
	if (!handles.empty())
		m_server->SendPacketMulticast(const_cast<unsigned long long*>(handles.data()), (long)handles.size(), packet);
	Packet::Free(packet);
}

void BattleContent::CollectView(GamePlayer* center, GamePlayer* exclude, std::vector<GamePlayer*>& out) const
{
	out.clear();
	if (center->sectorX < 0) return;
	m_sectors.ForEachInView(center->sectorX, center->sectorY, m_cfg.viewSectorRadius, [&](GamePlayer* q) {
		if (q != exclude) out.push_back(q);
	});
}

void BattleContent::Handles(const std::vector<GamePlayer*>& players, std::vector<unsigned long long>& out) const
{
	out.clear();
	out.reserve(players.size());
	for (GamePlayer* q : players) out.push_back(q->sessionHandle);
}

void BattleContent::WriteCreateEntry(Packet* pkt, GamePlayer* p) const
{
	W32(pkt, p->playerId);
	WName(pkt, p->name, NAME_LEN);
	WF(pkt, p->x); WF(pkt, p->z); WF(pkt, p->vx); WF(pkt, p->vz); WF(pkt, p->aim);
	W16(pkt, p->hp); W16(pkt, p->maxHp);
	W8(pkt, p->weaponId);
	W8(pkt, 0);
}

void BattleContent::SendCreateList(GamePlayer* to, const std::vector<GamePlayer*>& list)
{
	for (size_t start = 0; start < list.size(); start += MAX_CREATE_PER_PACKET)
	{
		size_t n = std::min(list.size() - start, (size_t)MAX_CREATE_PER_PACKET);
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_CREATE_CHARACTERS);
		W8(pkt, (uint8_t)n);
		for (size_t i = start; i < start + n; i++) WriteCreateEntry(pkt, list[i]);
		SendTo(to, pkt);
	}
}

void BattleContent::SendDeleteList(GamePlayer* to, const std::vector<GamePlayer*>& list)
{
	for (size_t start = 0; start < list.size(); start += MAX_DELETE_PER_PACKET)
	{
		size_t n = std::min(list.size() - start, (size_t)MAX_DELETE_PER_PACKET);
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_DELETE_CHARACTERS);
		W8(pkt, (uint8_t)n);
		for (size_t i = start; i < start + n; i++) W32(pkt, list[i]->playerId);
		SendTo(to, pkt);
	}
}

Packet* BattleContent::BuildCreateOne(GamePlayer* p) const
{
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_CREATE_CHARACTERS);
	W8(pkt, 1);
	WriteCreateEntry(pkt, p);
	return pkt;
}

Packet* BattleContent::BuildDeleteOne(GamePlayer* p) const
{
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_DELETE_CHARACTERS);
	W8(pkt, 1);
	W32(pkt, p->playerId);
	return pkt;
}

Packet* BattleContent::BuildRanking(const std::vector<RankEntry>& top) const
{
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_RANKING_TOP3);
	W8(pkt, (uint8_t)top.size());
	for (size_t i = 0; i < top.size(); i++)
	{
		GamePlayer* p = FindById(top[i].id);
		char16_t emptyName[NAME_LEN] = {};
		W8(pkt, (uint8_t)(i + 1));
		W32(pkt, top[i].id);
		WName(pkt, p ? p->name : emptyName, NAME_LEN);
		W32(pkt, top[i].score);
	}
	return pkt;
}

GamePlayer* BattleContent::FindById(uint32_t id) const
{
	auto it = m_byId.find(id);
	return it == m_byId.end() ? nullptr : it->second;
}
