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
	const int ONLINE_BROADCAST_MIN_MS = 200;   // 접속 인원 방송 최소 간격 (대량 접속 시 병합)

	bool Finite(float v) { return std::isfinite(v); }
	float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	float NormalizeAngle(float deg)
	{
		float a = fmodf(deg, 360.0f);
		if (a < 0) a += 360.0f;
		return a;
	}
}

std::atomic<int> BattleContent::s_online{ 0 };

BattleContent::BattleContent(int roomNo, const GameConfig& cfg, const WeaponTable& weapons, const SpawnTable& spawns, const ObstacleMap& obstacles)
	: NetLib_Content(cfg.battleTickMs), m_roomNo(roomNo), m_cfg(cfg), m_weapons(weapons), m_spawns(spawns), m_obstacles(obstacles),
	  m_rng((unsigned)(roomNo * 7919 + timeGetTime())),
	  m_bagCells(MapConst::SectorCount * MapConst::SectorCount)
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
	m_lastContainerCheck = m_lastUpdateTime;
	// 에어드랍: 서버 업타임이 airdrop_interval_ms의 배수가 될 때마다 (19.6)
	m_airdropActive = false;    // 빈 방에 첫 플레이어가 들어오면 시작 (22.4)
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
	p->equipped = SLOT_PISTOL;
	p->specialWeaponId = 0;
	p->specialDurability = 0;
	p->bandages = (uint8_t)std::clamp(m_cfg.startBandages, 0, std::max(0, m_cfg.maxBandages));
	p->rolling = p->rollCheckPending = false;
	p->rollUntil = p->rollReadyAt = now;
	CancelBandage(p);
	p->lastMovedTime = now;
	p->openContainerId = 0;
	ChooseSpawn(p->x, p->z);
	p->vx = p->vz = 0;
	p->aim = 0;
	p->RecordHistory(now);

	m_players[sessionHandle] = p;
	m_byId[p->playerId] = p;
	m_playerCount.store((int)m_players.size());
	s_online.fetch_add(1);
	m_sectors.Add(p, MapConst::ToSector(p->x), MapConst::ToSector(p->z));

	SendEnterSequence(p);

	// 빈 방에 첫 플레이어 → 에어드랍 즉시 1회차 + 주기 타이머 시작 (22.4)
	if (m_players.size() == 1 && m_cfg.airdropIntervalMs > 0)
	{
		m_airdropActive = true;
		m_nextAirdropAt = now + (uint32_t)m_cfg.airdropIntervalMs;
		AirdropRound(now);
	}

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

	// 1-1) PT_SC_PLAYER_COUNT (현재 서버 전체 인원, 이후 변경은 UpdateOnlineCount가 방송)
	Packet* cnt = Packet::NetAlloc();
	W16(cnt, PT_SC_PLAYER_COUNT);
	W32(cnt, (uint32_t)s_online.load());
	SendTo(p, cnt);

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
			WF(w, d.jitterDeg); W16(w, d.durability); W8(w, d.slot); W8(w, 0); W8(w, 0);   // v6 (36B)
		}
		SendTo(p, w);
	}

	// 2-1) PT_SC_INVENTORY
	SendInventory(p);

	// 3) PT_SC_CREATE_CHARACTERS (시야 내 기존 플레이어)
	std::vector<GamePlayer*> view;
	CollectView(p, p, view);
	SendCreateList(p, view);

	// 3-1) PT_SC_CONTAINER_CREATE (시야 안 가방), PT_SC_AIRDROP (기존 에어드랍, IsNew=0)
	std::vector<uint32_t> bags;
	CollectBagsInView(p->sectorX, p->sectorY, bags);
	SendContainerCreate(p, bags);
	std::vector<unsigned long long> self{ p->sessionHandle };
	for (auto& kv : m_containers)
		if (kv.second.type == CONTAINER_AIRDROP) SendAirdrop(self, kv.second, false);

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
	case PT_CS_ROLL:
		if (payloadLen != LEN_CS_ROLL) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleRoll(p, r, now);
		break;
	case PT_CS_SWITCH_WEAPON:
		if (payloadLen != LEN_CS_SWITCH_WEAPON) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleSwitch(p, r.U8(), now);
		break;
	case PT_CS_USE_BANDAGE:
		if (payloadLen != LEN_CS_USE_BANDAGE) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleBandage(p, now);
		break;
	case PT_CS_OPEN_CONTAINER:
		if (payloadLen != LEN_CS_OPEN_CONTAINER) { Kick(p, KICK_INVALID_PACKET, now); return; }
		HandleOpen(p, r.U32(), now);
		break;
	case PT_CS_TAKE_ITEM:
	{
		if (payloadLen != LEN_CS_TAKE_ITEM) { Kick(p, KICK_INVALID_PACKET, now); return; }
		uint32_t id = r.U32();
		HandleTake(p, id, r.U8(), now);
		break;
	}
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

	// 구르는 동안의 이동은 무시 (19.3). 구르기 끝 무렵(100ms 이내)에 도착한 이동은 도착 편차로 보고 받는다
	if (p->rolling)
	{
		if (TimeDiff(now, p->rollUntil) < -100) return;
		p->rolling = false;
	}
	// 구르기 후 첫 이동: 클라 도착점이 서버 계산과 1m 넘게 다르면 보정 (위반으로 세지 않음).
	// 구르기 직후의 이동이 패킷 뭉침으로 구르는 중에 도착해 무시되었을 수 있으므로, 구르기 종료 후 경과 시간만큼 이동 거리를 더 허용
	if (p->rollCheckPending)
	{
		p->rollCheckPending = false;
		if (Finite(px) && Finite(pz))
		{
			float ex = px - p->x, ez = pz - p->z;
			float allow = 1.0f + m_cfg.moveSpeed * m_cfg.sprintMultiplier * (float)std::max(0, TimeDiff(now, p->rollUntil)) / 1000.0f;
			if (ex * ex + ez * ez > allow * allow)
			{
				Packet* pkt = Packet::NetAlloc();
				W16(pkt, PT_SC_POSITION_CORRECT);
				WF(pkt, p->x); WF(pkt, p->z);
				W16(pkt, seq);
				SendTo(p, pkt);
				return;
			}
		}
	}

	uint8_t viol = VIOL_NONE;
	if (!(Finite(px) && Finite(pz) && Finite(vx) && Finite(vz) && Finite(aim))) viol = VIOL_MOVE_VALUE;
	float tx = px, tz = pz;
	bool clamped = false;
	if (!viol)
	{
		tx = Clampf(px, MapConst::MinPos, MapConst::MaxPos);
		tz = Clampf(pz, MapConst::MinPos, MapConst::MaxPos);
		clamped = (tx != px || tz != pz);

		float speed = sqrtf(vx * vx + vz * vz);
		if (speed > m_cfg.moveSpeed * m_cfg.sprintMultiplier * 1.1f) viol = VIOL_MOVE_SPEED;   // 달리기 포함
	}
	// 엄폐물: 이동 경로(중심선)와 도착 위치(원)가 막힌 셀과 겹치면 거부
	if (!viol && (m_obstacles.SegmentBlocked(p->x, p->z, tx, tz, false) ||
	              m_obstacles.CircleBlocked(tx, tz, m_cfg.characterRadius - 0.1f)))
		viol = VIOL_MOVE_BLOCKED;
	if (!viol)
	{
		float dx = tx - p->x, dz = tz - p->z;
		float d = sqrtf(dx * dx + dz * dz);
		if (d > p->moveBudget + 0.5f) viol = VIOL_MOVE_BUDGET;
		else p->moveBudget = std::max(0.0f, p->moveBudget - d);
	}

	if (viol)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_POSITION_CORRECT);
		WF(pkt, p->x); WF(pkt, p->z);
		W16(pkt, seq);
		SendTo(p, pkt);
		CountMoveViolation(p, now, viol);
		return;
	}

	if (fabsf(tx - p->x) > 0.01f || fabsf(tz - p->z) > 0.01f || vx * vx + vz * vz > 0.01f)
		p->lastMovedTime = now;
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
	uint8_t spreadSeed = r.U8();
	r.U8();     // reserved

	// 구르는 중 사격은 클라가 막는다. 패킷 뭉침을 감안해 명백히 이른 것만 조용히 버린다 (19.3)
	if (p->rolling && TimeDiff(p->rollUntil, now) > 150) return;

	const WeaponDef* w = m_weapons.Find(weaponId);
	uint8_t viol = VIOL_NONE;
	float len = 0;
	int32_t rewind = TimeDiff(now, viewTime);
	if (w == nullptr || weaponId != p->weaponId) viol = VIOL_FIRE_WEAPON;       // 장착 무기만 (19.1)
	else if (shotSeq <= p->lastShotSeq) viol = VIOL_FIRE_SEQ;
	else if (!(Finite(ox) && Finite(oz) && Finite(dx) && Finite(dz))) viol = VIOL_FIRE_VALUE;
	else
	{
		len = sqrtf(dx * dx + dz * dz);
		float ex = ox - p->x, ez = oz - p->z;
		if (len < 0.9f || len > 1.1f) viol = VIOL_FIRE_DIR;
		else if (sqrtf(ex * ex + ez * ez) > FIRE_ORIGIN_TOLERANCE) viol = VIOL_FIRE_ORIGIN;
		else if (rewind < -FUTURE_VIEWTIME_TOLERANCE_MS) viol = VIOL_FIRE_FUTURE;
		else if (p->fireTokens < 1.0f) viol = VIOL_FIRE_RATE;
	}
	if (viol)
	{
		CountCheat(p, now, viol);
		return;
	}

	if (rewind > m_cfg.maxRewindMs) viewTime = now - (uint32_t)m_cfg.maxRewindMs;
	if (rewind < 0) viewTime = now;
	p->fireTokens -= 1.0f;
	p->lastShotSeq = shotSeq;
	dx /= len; dz /= len;
	CancelBandage(p);

	// 특수 총 내구도 (19.1): 0이 되면 사라지고 권총으로 전환
	if (p->equipped == SLOT_SPECIAL && w->durability > 0)
	{
		if (p->specialDurability > 0) p->specialDurability--;
		if (p->specialDurability == 0)
		{
			p->specialWeaponId = 0;
			p->equipped = SLOT_PISTOL;
			p->weaponId = (uint8_t)m_cfg.defaultWeaponId;
			const WeaponDef* pw = m_weapons.Find(p->weaponId);
			if (pw) p->fireTokens = std::min(p->fireTokens, FireTokenCap(pw));
			SendInventory(p);
		}
	}

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
		W8(pkt, spreadSeed);    // 사수 클라와 같은 산탄 각도 (19.2)
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
		int res = ValidateHit(p, shotSeq, pellet, targetId, hx, hz, now, &target, &w);
		if (res != 0)
		{
			// 대상 사망·시야 이탈·보고 시한 초과 등 정상 플레이에서도 생기는 거부는 세지 않는다 (10.3)
			if (res < 0) { p->ignoredHits++; continue; }
			CountCheat(p, now, (uint8_t)res);
			if (p->disconnecting) return;
			continue;
		}
		ShotRecord* s = p->FindShot(shotSeq);
		s->hitTargets[pellet][s->hitCount[pellet]++] = targetId;
		ApplyDamage(p, target, shotSeq, w->damage, now);
	}
}

int BattleContent::ValidateHit(GamePlayer* shooter, uint32_t shotSeq, uint8_t pellet, uint32_t targetId,
                               float hx, float hz, uint32_t now, GamePlayer** outTarget, const WeaponDef** outWeapon)
{
	const int HIT_IGNORE = -1;     // 정상 플레이에서도 생기는 거부 (세지 않음, 10.3)

	// 1. 사격 기록 존재. 서버가 거부했거나 오래된 사격(seq ≤ 마지막 사격)은 세지 않고, 보낸 적 없는 미래 seq만 위반
	ShotRecord* s = shooter->FindShot(shotSeq);
	if (s == nullptr) return shotSeq > shooter->lastShotSeq ? VIOL_HIT_FAKE_SHOT : HIT_IGNORE;
	const WeaponDef* w = m_weapons.Find(s->weaponId);
	if (w == nullptr) return VIOL_HIT_WEAPON;

	// 2. 펠릿 번호
	if (pellet >= w->pellets || pellet >= ShotRecord::MAX_PELLETS) return VIOL_HIT_PELLET;

	// 3. 관통 수 / 중복 대상
	int maxTargets = std::min(1 + (int)w->pierce, ShotRecord::MAX_TARGETS);
	if (s->hitCount[pellet] >= maxTargets) return VIOL_HIT_PIERCE;
	for (int i = 0; i < s->hitCount[pellet]; i++)
		if (s->hitTargets[pellet][i] == targetId) return VIOL_HIT_DUP;

	// 4. 대상 상태 / 시야: 대상이 이미 죽었거나 나갔거나 시야 밖이면 세지 않음
	GamePlayer* t = FindById(targetId);
	if (t == shooter) return VIOL_HIT_SELF;
	if (t == nullptr || !t->IsAlive()) return HIT_IGNORE;
	if (!SectorMap::IsNear(shooter->sectorX, shooter->sectorY, t->sectorX, t->sectorY, m_cfg.viewSectorRadius)) return HIT_IGNORE;

	// 5. 보고 시한 (지연·패킷 뭉침으로 늦을 수 있음 → 세지 않음)
	float flightMs = w->range / w->projectileSpeed * 1000.0f;
	if ((float)TimeDiff(now, s->recvTime) > flightMs + m_cfg.maxRewindMs + 200.0f) return HIT_IGNORE;

	// 6. 거리
	if (!Finite(hx) || !Finite(hz)) return VIOL_HIT_VALUE;
	float rx = hx - s->ox, rz = hz - s->oz;
	float dist = sqrtf(rx * rx + rz * rz);
	if (dist > w->range + w->projectileRadius) return VIOL_HIT_RANGE;

	// 7. 각도
	if (dist >= 2.0f)
	{
		float cosA = (rx * s->dx + rz * s->dz) / dist;
		cosA = Clampf(cosA, -1.0f, 1.0f);
		float angleDeg = acosf(cosA) * 180.0f / PI;
		if (angleDeg > w->spreadDeg * 0.5f + 3.0f) return VIOL_HIT_ANGLE;
	}

	// 8. 과거 위치 일치 (대상 이력이 아직 없는 시각이면 판정 불가 → 세지 않음)
	uint32_t tHit = s->viewTime + (uint32_t)(dist / w->projectileSpeed * 1000.0f);
	float px, pz;
	if (!t->history.PosAt(tHit, px, pz)) return HIT_IGNORE;
	float ex = hx - px, ez = hz - pz;
	if (sqrtf(ex * ex + ez * ez) > m_cfg.characterRadius + w->projectileRadius + m_cfg.hitTolerance) return VIOL_HIT_POSITION;

	// 9. 사선: 발사 위치 → 충돌 지점 사이에 벽(WALL)이 없어야 한다 (낮은 엄폐물은 통과)
	if (m_obstacles.SegmentBlocked(s->ox, s->oz, hx, hz, true)) return VIOL_HIT_WALL;

	*outTarget = t;
	*outWeapon = w;
	return 0;
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
	CancelBandage(victim);
	victim->openContainerId = 0;
	CreateBag(victim, now);     // 사망 위치에 가방 (19.7)
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
	LogKickDetail(p, reason);
	ScheduleDisconnect(p, now + 300);   // 킥 패킷이 나갈 시간을 준다
}

void BattleContent::ScheduleDisconnect(GamePlayer* p, uint32_t at)
{
	if (p->disconnecting) return;
	p->disconnecting = true;
	m_pending.push_back({ p->sessionHandle, at });
}

void BattleContent::CountCheat(GamePlayer* p, uint32_t now, uint8_t kind)
{
	if (kind < VIOL_KIND_COUNT && p->violationCounts[kind] < 0xFFFF) p->violationCounts[kind]++;
	p->lastCheatViolation = kind;
	if (p->cheatViolations.Add(now, CHEAT_WINDOW_MS, CHEAT_LIMIT))
		Kick(p, KICK_CHEAT_SUSPECT, now);
}

void BattleContent::CountMoveViolation(GamePlayer* p, uint32_t now, uint8_t kind)
{
	if (kind < VIOL_KIND_COUNT && p->violationCounts[kind] < 0xFFFF) p->violationCounts[kind]++;
	p->lastMoveViolation = kind;
	if (p->moveViolations.Add(now, MOVE_VIOLATION_WINDOW_MS, MOVE_VIOLATION_LIMIT))
		Kick(p, KICK_CHEAT_SUSPECT, now);
}

// 킥 로그 (10.4): 사유, 이동/부정 카운터(기간 내 횟수·마지막 종류), 입장 후 종류별 누적, 세지 않은 명중 거부 수
void BattleContent::LogKickDetail(GamePlayer* p, uint8_t reason)
{
	static const char* kickNames[] = { "-", "TIMEOUT", "INVALID_PACKET", "CHEAT_SUSPECT", "SERVER_SHUTDOWN" };
	char counts[320];
	int n = 0;
	counts[0] = 0;
	for (int k = 1; k < VIOL_KIND_COUNT && n < (int)sizeof(counts) - 40; k++)
		if (p->violationCounts[k])
			n += snprintf(counts + n, sizeof(counts) - n, "%s%s=%u", n ? " " : "", ViolationName((uint8_t)k), (unsigned)p->violationCounts[k]);
	GameLog("[room %d] kick player %u %s: move %d/%ds last %s, cheat %d/%ds last %s | %s | ignored-hit %u, in-game %ums",
		m_roomNo, p->playerId, reason < 5 ? kickNames[reason] : "?",
		p->moveViolations.count, MOVE_VIOLATION_WINDOW_MS / 1000, ViolationName(p->lastMoveViolation),
		p->cheatViolations.count, CHEAT_WINDOW_MS / 1000, ViolationName(p->lastCheatViolation),
		n ? counts : "no violations", p->ignoredHits, (unsigned)TimeDiff(GetServerTimeMs(), p->enterTime));
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

	// 가방 시야 diff (19.7)
	{
		std::vector<uint32_t> oldBags, newBags, bagRemoved, bagAdded;
		CollectBagsInView(osx, osy, oldBags);
		CollectBagsInView(nsx, nsy, newBags);
		for (uint32_t id : oldBags)
			if (std::find(newBags.begin(), newBags.end(), id) == newBags.end()) bagRemoved.push_back(id);
		for (uint32_t id : newBags)
			if (std::find(oldBags.begin(), oldBags.end(), id) == oldBags.end()) bagAdded.push_back(id);
		SendContainerDelete(p, bagRemoved);
		SendContainerCreate(p, bagAdded);
	}

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
			p->fireTokens = std::min(FireTokenCap(w), p->fireTokens + dt * (1000.0f / w->fireIntervalMs) * 1.1f);

		// 구르기 종료
		if (p->rolling && TimeDiff(now, p->rollUntil) >= 0) p->rolling = false;

		// 붕대 완료 (19.4)
		if (p->usingBandage && TimeDiff(now, p->bandageUntil) >= 0)
		{
			CancelBandage(p);
			if (p->bandages > 0 && p->hp < p->maxHp && !p->disconnecting)
			{
				p->bandages--;
				p->hp = (uint16_t)std::min<int>(p->maxHp, p->hp + m_cfg.bandageHeal);
				SendHp(p);
				SendInventory(p);
			}
		}

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

	UpdateContainers(now);
	UpdateRanking(false);
	UpdateOnlineCount(now);
}

// 서버 전체 접속 인원이 바뀌었으면 방 전체에 방송 (최소 간격 ONLINE_BROADCAST_MIN_MS로 병합)
void BattleContent::UpdateOnlineCount(uint32_t now)
{
	int total = s_online.load();
	if (total == m_lastSentOnline || TimeDiff(now, m_lastOnlineSend) < ONLINE_BROADCAST_MIN_MS) return;
	m_lastSentOnline = total;
	m_lastOnlineSend = now;

	std::vector<unsigned long long> hs;
	hs.reserve(m_players.size());
	for (auto& kv : m_players)
		if (!kv.second->disconnecting) hs.push_back(kv.first);
	if (hs.empty()) return;
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_PLAYER_COUNT);
	W32(pkt, (uint32_t)total);
	Multicast(hs, pkt);
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
	s_online.fetch_sub(1);
	m_reserved.fetch_sub(1);
	GameLog("[room %d] player %u left (code %d, score %u)", m_roomNo, p->playerId, (int)code, p->score);
	delete p;
	if (m_players.empty()) m_airdropActive = false;    // 방이 비면 타이머 정지 (남은 에어드랍은 유지)
}

////////////////////////////////////////////////////////////////////////
// v6: 구르기 · 무기 전환 · 붕대 · 가방 · 에어드랍 (game-spec 19)
////////////////////////////////////////////////////////////////////////
float BattleContent::FireTokenCap(const WeaponDef* w) const
{
	// 연사 간격이 긴 무기는 토큰을 적게 쌓는다 (전환 직후 연속 발사 방지). 권총(250ms) = 3
	if (w == nullptr) return FIRE_TOKEN_CAP;
	return Clampf(1.0f + 500.0f / (float)w->fireIntervalMs, 1.0f, FIRE_TOKEN_CAP);
}

void BattleContent::HandleRoll(GamePlayer* p, PayloadReader& r, uint32_t now)
{
	float sx = r.F(), sz = r.F(), dx = r.F(), dz = r.F();
	bool ok = Finite(sx) && Finite(sz) && Finite(dx) && Finite(dz);
	float len = ok ? sqrtf(dx * dx + dz * dz) : 0;
	ok = ok && len >= 0.9f && len <= 1.1f;
	if (!ok)
	{
		CountCheat(p, now, VIOL_ROLL_DIR);
		return;
	}
	// 쿨타임 (패킷 도착 편차 200ms 허용). 거부 시 서버 위치로 보정
	if (p->rolling || TimeDiff(now + 200, p->rollReadyAt) < 0)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_POSITION_CORRECT);
		WF(pkt, p->x); WF(pkt, p->z);
		W16(pkt, 0);
		SendTo(p, pkt);
		CountMoveViolation(p, now, VIOL_ROLL_COOLDOWN);
		return;
	}
	dx /= len; dz /= len;

	// 시작 위치: 클라 값이 서버 위치 3m 이내이고 막히지 않았으면 채택 (클라와 같은 계산을 위해)
	float ox = p->x, oz = p->z;
	sx = Clampf(sx, MapConst::MinPos, MapConst::MaxPos);
	sz = Clampf(sz, MapConst::MinPos, MapConst::MaxPos);
	{
		float ex = sx - p->x, ez = sz - p->z;
		if (ex * ex + ez * ez <= 9.0f && !m_obstacles.SegmentBlocked(p->x, p->z, sx, sz, false) &&
			!m_obstacles.CircleBlocked(sx, sz, m_cfg.characterRadius - 0.1f))
		{
			ox = sx; oz = sz;
		}
	}

	// 도착점: 0.25m 단위로 전진, 엄폐물(반지름 그대로)에 닿으면 그 앞에서 멈춤 (클라 LocalPlayerController와 동일)
	const float speed = m_cfg.moveSpeed * m_cfg.rollSpeedMult;
	const float dist = speed * (float)m_cfg.rollMs / 1000.0f;
	int steps = std::max(1, (int)ceilf(dist / 0.25f));
	float stepLen = dist / (float)steps;
	float cx = ox, cz = oz;
	for (int i = 0; i < steps; i++)
	{
		float nx = Clampf(cx + dx * stepLen, MapConst::MinPos, MapConst::MaxPos);
		float nz = Clampf(cz + dz * stepLen, MapConst::MinPos, MapConst::MaxPos);
		if (m_obstacles.CircleBlocked(nx, nz, m_cfg.characterRadius)) break;
		cx = nx; cz = nz;
	}

	CancelBandage(p);
	p->rolling = true;
	p->rollUntil = now + (uint32_t)m_cfg.rollMs;
	p->rollReadyAt = now + (uint32_t)m_cfg.rollCooldownMs;
	p->rollCheckPending = true;
	p->lastMovedTime = p->rollUntil;

	// 위치 이력: 시작(now) → 도착(now + roll_ms). 되감기 판정이 구르는 경로를 따른다
	p->history.Add(now, ox, oz, dx * speed, dz * speed);
	p->history.Add(p->rollUntil, cx, cz, 0, 0);
	p->x = cx; p->z = cz;
	p->vx = p->vz = 0;

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
		W16(pkt, PT_SC_ROLL);
		W32(pkt, p->playerId);
		WF(pkt, ox); WF(pkt, oz); WF(pkt, cx); WF(pkt, cz);
		Multicast(hs, pkt);
	}
}

void BattleContent::HandleSwitch(GamePlayer* p, uint8_t slot, uint32_t now)
{
	if (slot != SLOT_SPECIAL && slot != SLOT_PISTOL) { CountCheat(p, now, VIOL_SLOT); return; }
	CancelBandage(p);
	if (slot == SLOT_SPECIAL && p->specialWeaponId == 0)
	{
		SendInventory(p);   // 클라 상태 불일치(내구도 소진 직후 등) → 재동기화
		return;
	}
	if (p->equipped == slot) return;
	p->equipped = slot;
	p->weaponId = (slot == SLOT_SPECIAL) ? p->specialWeaponId : (uint8_t)m_cfg.defaultWeaponId;
	p->fireTokens = std::min(p->fireTokens, FireTokenCap(m_weapons.Find(p->weaponId)));
}

void BattleContent::HandleBandage(GamePlayer* p, uint32_t now)
{
	if (p->usingBandage || p->bandages == 0 || p->hp >= p->maxHp || p->rolling) return;
	p->usingBandage = true;
	p->bandageUntil = now + (uint32_t)m_cfg.bandageMs;
}

void BattleContent::HandleOpen(GamePlayer* p, uint32_t id, uint32_t now)
{
	auto it = m_containers.find(id);
	if (it == m_containers.end())
	{
		std::vector<uint32_t> gone{ id };
		SendContainerDelete(p, gone);   // 이미 사라짐 → 클라 정리
		return;
	}
	const Container& c = it->second;
	float ex = c.x - p->x, ez = c.z - p->z;
	float maxD = m_cfg.interactRange + 0.5f;
	if (ex * ex + ez * ez > maxD * maxD) return;
	if (c.type == CONTAINER_BAG && !SectorMap::IsNear(p->sectorX, p->sectorY, c.sx, c.sy, m_cfg.viewSectorRadius)) return;
	int hold = (c.type == CONTAINER_BAG) ? m_cfg.bagOpenMs : m_cfg.airdropOpenMs;
	if (TimeDiff(now, p->lastMovedTime) < hold - 250) return;     // 움직이지 않고 누르고 있었는가
	p->openContainerId = id;
	SendContents(p, c);
}

void BattleContent::HandleTake(GamePlayer* p, uint32_t id, uint8_t item, uint32_t now)
{
	auto it = m_containers.find(id);
	if (it == m_containers.end())
	{
		if (p->openContainerId == id) p->openContainerId = 0;
		std::vector<uint32_t> gone{ id };
		SendContainerDelete(p, gone);
		return;
	}
	Container& c = it->second;
	if (p->openContainerId != id) return;
	float ex = c.x - p->x, ez = c.z - p->z;
	float maxD = m_cfg.interactRange + 1.0f;
	if (ex * ex + ez * ez > maxD * maxD) { p->openContainerId = 0; return; }

	bool changed = false;
	if (item == ITEM_SPECIAL_WEAPON)
	{
		if (c.specialId != 0)
		{
			// 기존 특수 총은 덮어쓴다(버려짐). 장착 상태는 유지
			p->specialWeaponId = c.specialId;
			p->specialDurability = c.durability;
			c.specialId = 0;
			c.durability = 0;
			if (p->equipped == SLOT_SPECIAL)
			{
				p->weaponId = p->specialWeaponId;
				p->fireTokens = std::min(p->fireTokens, FireTokenCap(m_weapons.Find(p->weaponId)));
			}
			changed = true;
		}
	}
	else if (item == ITEM_BANDAGE)
	{
		int room = std::max(0, m_cfg.maxBandages - (int)p->bandages);
		int take = std::min<int>(room, c.bandages);
		if (take > 0)
		{
			p->bandages = (uint8_t)(p->bandages + take);
			c.bandages = (uint8_t)(c.bandages - take);
			changed = true;
		}
	}
	else
	{
		CountCheat(p, now, VIOL_ITEM);
		return;
	}

	if (!changed)
	{
		SendContents(p, c);     // 먼저 가져간 사람이 있음 → 최신 내용
		return;
	}
	SendInventory(p);
	BroadcastContents(c);
	// 에어드랍: 누군가 특수 총을 가져가면 즉시 제거(남은 붕대도 사라짐), 붕대만 가져가 비어도 제거 (22.4)
	if (c.type == CONTAINER_AIRDROP && (item == ITEM_SPECIAL_WEAPON || (c.specialId == 0 && c.bandages == 0)))
		RemoveContainer(id);
}

void BattleContent::CreateBag(GamePlayer* victim, uint32_t now)
{
	Container c{};
	c.id = m_nextContainerId++;
	if (m_nextContainerId == 0) m_nextContainerId = 1;
	c.type = CONTAINER_BAG;
	c.x = victim->x; c.z = victim->z;
	c.sx = MapConst::ToSector(c.x); c.sy = MapConst::ToSector(c.z);
	c.specialId = victim->specialWeaponId;
	c.durability = victim->specialWeaponId ? victim->specialDurability : 0;
	c.bandages = victim->bandages;
	c.expireAt = now + (uint32_t)m_cfg.bagLifetimeMs;
	m_containers[c.id] = c;
	m_bagCells[c.sy * MapConst::SectorCount + c.sx].push_back(c.id);

	std::vector<unsigned long long> hs;
	m_sectors.ForEachInView(c.sx, c.sy, m_cfg.viewSectorRadius, [&](GamePlayer* q) { hs.push_back(q->sessionHandle); });
	if (hs.empty()) return;
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_CONTAINER_CREATE);
	W8(pkt, 1);
	W32(pkt, c.id); W8(pkt, c.type); WF(pkt, c.x); WF(pkt, c.z);
	Multicast(hs, pkt);
}

void BattleContent::RemoveContainer(uint32_t id)
{
	auto it = m_containers.find(id);
	if (it == m_containers.end()) return;
	Container c = it->second;
	m_containers.erase(it);

	std::vector<unsigned long long> hs;
	if (c.type == CONTAINER_BAG)
	{
		auto& cell = m_bagCells[c.sy * MapConst::SectorCount + c.sx];
		auto ci = std::find(cell.begin(), cell.end(), id);
		if (ci != cell.end()) { *ci = cell.back(); cell.pop_back(); }
		m_sectors.ForEachInView(c.sx, c.sy, m_cfg.viewSectorRadius, [&](GamePlayer* q) { hs.push_back(q->sessionHandle); });
	}
	else
	{
		AllHandles(hs);
	}
	for (auto& kv : m_players)
		if (kv.second->openContainerId == id) kv.second->openContainerId = 0;
	if (hs.empty()) return;
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_CONTAINER_DELETE);
	W8(pkt, 1);
	W32(pkt, id);
	Multicast(hs, pkt);
}

void BattleContent::UpdateContainers(uint32_t now)
{
	// 가방 만료 (0.5초마다 검사)
	if (TimeDiff(now, m_lastContainerCheck) >= 500)
	{
		m_lastContainerCheck = now;
		std::vector<uint32_t> expired;
		for (auto& kv : m_containers)
			if (kv.second.type == CONTAINER_BAG && TimeDiff(now, kv.second.expireAt) >= 0) expired.push_back(kv.first);
		for (uint32_t id : expired) RemoveContainer(id);
	}

	// 에어드랍 회차 (22.4)
	if (m_airdropActive && m_cfg.airdropIntervalMs > 0 && TimeDiff(now, m_nextAirdropAt) >= 0)
	{
		while (TimeDiff(now, m_nextAirdropAt) >= 0) m_nextAirdropAt += (uint32_t)m_cfg.airdropIntervalMs;
		AirdropRound(now);
	}
}

// 방 인원 기준 최대 개수 = min(airdrop_max, ceil(인원 / airdrop_players_per)), 최소 1
int BattleContent::AirdropCap() const
{
	int n = (int)m_players.size();
	int per = std::max(1, m_cfg.airdropPlayersPer);
	int cap = std::max(1, (n + per - 1) / per);
	return std::min(cap, m_cfg.airdropMax);
}

// 한 회차: 남은 자리(최대 - 현재) 중 1~남은 자리 무작위 개수 생성, 남은 자리가 없으면 건너뜀
void BattleContent::AirdropRound(uint32_t now)
{
	int existing = 0;
	for (auto& kv : m_containers)
		if (kv.second.type == CONTAINER_AIRDROP) existing++;
	int cap = AirdropCap();
	int remain = cap - existing;
	if (remain <= 0)
	{
		GameLog("[room %d] airdrop round skipped (%d / cap %d)", m_roomNo, existing, cap);
		return;
	}
	int count = std::uniform_int_distribution<int>(1, remain)(m_rng);
	for (int i = 0; i < count; i++)
		if (!TryAirdrop(now)) break;
}

bool BattleContent::TryAirdrop(uint32_t now)
{
	std::vector<std::pair<int, int>> existing;
	for (auto& kv : m_containers)
		if (kv.second.type == CONTAINER_AIRDROP) existing.push_back({ kv.second.sx, kv.second.sy });

	const int N = MapConst::SectorCount;
	std::vector<int> alive(N * N, 0);
	int total = 0;
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
		{
			int n = 0;
			for (GamePlayer* q : m_sectors.Cell(x, y)) if (q->IsAlive() && !q->disconnecting) n++;
			alive[y * N + x] = n;
			total += n;
		}
	if (total == 0) return false;

	// 3x3 묶음(가운데 섹터 기준) 중 생존 인원 최다. 기존 에어드랍과 체비셰프 거리 3 미만 제외
	int best = -1;
	std::vector<int> cands;
	for (int cy = 0; cy < N; cy++)
		for (int cx = 0; cx < N; cx++)
		{
			bool tooClose = false;
			for (auto& e : existing)
				if (std::max(abs(e.first - cx), abs(e.second - cy)) < 3) { tooClose = true; break; }
			if (tooClose) continue;
			int sum = 0;
			for (int y = cy - 1; y <= cy + 1; y++)
				for (int x = cx - 1; x <= cx + 1; x++)
					if (SectorMap::InRange(x, y)) sum += alive[y * N + x];
			if (sum > best) { best = sum; cands.clear(); }
			if (sum == best) cands.push_back(cy * N + cx);
		}
	if (cands.empty()) return false;
	int pick = cands[std::uniform_int_distribution<int>(0, (int)cands.size() - 1)(m_rng)];
	int csx = pick % N, csy = pick / N;

	const float S = (float)MapConst::SectorSize;
	const float margin = 4.0f;
	std::uniform_real_distribution<float> ux(csx * S + margin, (csx + 1) * S - margin), uz(csy * S + margin, (csy + 1) * S - margin);
	float x = Clampf(ux(m_rng), MapConst::MinPos, MapConst::MaxPos);
	float z = Clampf(uz(m_rng), MapConst::MinPos, MapConst::MaxPos);
	m_obstacles.FindFree(x, z, 1.0f, 20.0f);

	Container c{};
	c.id = m_nextContainerId++;
	if (m_nextContainerId == 0) m_nextContainerId = 1;
	c.type = CONTAINER_AIRDROP;
	c.x = x; c.z = z;
	c.sx = MapConst::ToSector(x); c.sy = MapConst::ToSector(z);
	auto specials = m_weapons.Specials();
	if (!specials.empty())
	{
		const WeaponDef* w = specials[std::uniform_int_distribution<int>(0, (int)specials.size() - 1)(m_rng)];
		c.specialId = w->id;
		c.durability = w->durability;
	}
	c.bandages = (uint8_t)std::clamp(m_cfg.maxBandages, 0, 255);
	c.expireAt = 0;
	m_containers[c.id] = c;

	std::vector<unsigned long long> hs;
	AllHandles(hs);
	SendAirdrop(hs, c, true);
	GameLog("[room %d] airdrop %u at sector (%d,%d) pos (%.1f, %.1f), group alive %d, weapon %d",
		m_roomNo, c.id, c.sx, c.sy, c.x, c.z, best, c.specialId);
	return true;
}

void BattleContent::SendAirdrop(const std::vector<unsigned long long>& hs, const Container& c, bool isNew)
{
	if (hs.empty()) return;
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_AIRDROP);
	W32(pkt, c.id);
	WF(pkt, c.x); WF(pkt, c.z);
	W8(pkt, (uint8_t)c.sx); W8(pkt, (uint8_t)c.sy);
	W8(pkt, isNew ? 1 : 0);
	Multicast(hs, pkt);
}

void BattleContent::SendInventory(GamePlayer* p)
{
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_INVENTORY);
	W8(pkt, p->equipped);
	W8(pkt, p->specialWeaponId);
	W16(pkt, p->specialWeaponId ? p->specialDurability : 0);
	W8(pkt, p->bandages);
	SendTo(p, pkt);
}

void BattleContent::SendHp(GamePlayer* p)
{
	std::vector<GamePlayer*> view;
	CollectView(p, nullptr, view);
	if (view.empty()) return;
	std::vector<unsigned long long> hs;
	Handles(view, hs);
	Packet* pkt = Packet::Alloc();
	W16(pkt, PT_SC_HP);
	W32(pkt, p->playerId);
	W16(pkt, p->hp);
	Multicast(hs, pkt);
}

void BattleContent::SendContents(GamePlayer* to, const Container& c)
{
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_CONTAINER_CONTENTS);
	W32(pkt, c.id);
	W8(pkt, c.specialId);
	W16(pkt, c.specialId ? c.durability : 0);
	W8(pkt, c.bandages);
	SendTo(to, pkt);
}

void BattleContent::BroadcastContents(const Container& c)
{
	for (auto& kv : m_players)
	{
		GamePlayer* q = kv.second;
		if (q->openContainerId == c.id && !q->disconnecting) SendContents(q, c);
	}
}

void BattleContent::CollectBagsInView(int sx, int sy, std::vector<uint32_t>& out) const
{
	out.clear();
	if (sx < 0) return;
	const int r = m_cfg.viewSectorRadius;
	for (int y = sy - r; y <= sy + r; y++)
		for (int x = sx - r; x <= sx + r; x++)
			if (SectorMap::InRange(x, y))
			{
				const auto& cell = m_bagCells[y * MapConst::SectorCount + x];
				out.insert(out.end(), cell.begin(), cell.end());
			}
}

void BattleContent::SendContainerCreate(GamePlayer* to, const std::vector<uint32_t>& ids)
{
	for (size_t start = 0; start < ids.size(); start += MAX_CONTAINER_CREATE_PER_PACKET)
	{
		size_t n = std::min(ids.size() - start, (size_t)MAX_CONTAINER_CREATE_PER_PACKET);
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_CONTAINER_CREATE);
		W8(pkt, (uint8_t)n);
		for (size_t i = start; i < start + n; i++)
		{
			const Container& c = m_containers.at(ids[i]);
			W32(pkt, c.id); W8(pkt, c.type); WF(pkt, c.x); WF(pkt, c.z);
		}
		SendTo(to, pkt);
	}
}

void BattleContent::SendContainerDelete(GamePlayer* to, const std::vector<uint32_t>& ids)
{
	for (size_t start = 0; start < ids.size(); start += MAX_CONTAINER_DELETE_PER_PACKET)
	{
		size_t n = std::min(ids.size() - start, (size_t)MAX_CONTAINER_DELETE_PER_PACKET);
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_CONTAINER_DELETE);
		W8(pkt, (uint8_t)n);
		for (size_t i = start; i < start + n; i++) W32(pkt, ids[i]);
		SendTo(to, pkt);
	}
}

void BattleContent::AllHandles(std::vector<unsigned long long>& out) const
{
	out.clear();
	out.reserve(m_players.size());
	for (auto& kv : m_players)
		if (!kv.second->disconnecting) out.push_back(kv.first);
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
