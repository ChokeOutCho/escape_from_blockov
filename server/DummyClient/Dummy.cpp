#include "Dummy.h"
#include "Network.h"
#include "EventLog.h"
#include <cmath>
#include <algorithm>

namespace
{
	const float PI_F = 3.14159265f;
	const int ENTER_TIMEOUT_MS = 10000;
	const int MOVE_SEND_MS = 100;           // 이동 중 CS_MOVE 주기 (클라와 동일)
	const int MOVE_MIN_MS = 50;             // 속도 변화 시 최소 간격
	const float AIM_SEND_DEG = 5.0f;
	const int PING_MS = 2000;
	const int HEARTBEAT_MS = 60000;
	const int REMOTE_EXTRAPOLATE_MAX_MS = 200;
	const int MAX_LOS_CHECKS = 4;           // 대상 선택 시 시야선(벽) 검사 최대 횟수
	const int HIT_STALE_MS = 1500;          // 보고 시한을 넘긴 명중은 버린다

	float AngleDiff(float a, float b)
	{
		float d = fmodf(fabsf(a - b), 360.0f);
		return d > 180.0f ? 360.0f - d : d;
	}
	float ToAim(float dx, float dz)
	{
		float a = atan2f(dz, dx) * 180.0f / PI_F;
		return a < 0 ? a + 360.0f : a;
	}
}

void Dummy::Init(int idx)
{
	index = idx;
	InitializeSRWLock(&lock);
	m_rng.seed((uint32_t)idx * 2654435761u ^ NowMs());
	float lo = std::clamp(g_cfg.weaknessMin, 0.0f, 1.0f), hi = std::clamp(g_cfg.weaknessMax, 0.0f, 1.0f);
	if (hi < lo) std::swap(lo, hi);
	m_weak = lo + (hi - lo) * Rand01();
	m_reconnectAt = NowMs() + (uint32_t)RandInt(0, 500);
}

void Dummy::Send(PacketWriter& w)
{
	if (!w.Ok()) return;
	const char* data = w.Data();
	g_net.Send(this, data, w.Size());
}

////////////////////////////////////////////////////////////////////////
// 로직 틱
////////////////////////////////////////////////////////////////////////
void Dummy::Tick(uint32_t now)
{
	bool wanted = index < g_targetCount.load(std::memory_order_relaxed) && !m_retired;

	if (net == NetState::Idle)
	{
		if (wanted && TimeDiff(now, m_reconnectAt) >= 0 && TakeConnectToken())
		{
			if (!g_net.Connect(this, now))
				m_reconnectAt = now + 1000 + (uint32_t)RandInt(0, 1000);
		}
		return;
	}
	if (!wanted && (net == NetState::Connected || net == NetState::Connecting))
	{
		g_net.Close(this, true, now, "target count reduced");     // 인원 축소
		return;
	}
	if (net != NetState::Connected) return;

	switch (game)
	{
	case GameState::Entering:
		if (TimeDiff(now, m_enterDeadline) > 0)
		{
			g_stats.enterTimeout++;
			m_reconnectAt = now + 3000;
			g_net.Close(this, false, now, "enter timeout (no SC_ENTER_GAME in 10s)");
		}
		break;
	case GameState::InGame:
		GameTick(now);
		break;
	case GameState::Dead:
		// 서버가 death_disconnect_ms(3초) 뒤 끊는다. 너무 오래 걸리면 직접 끊는다
		if (TimeDiff(now, m_deadAt) > 10000) g_net.Close(this, true, now, "dead but not disconnected by server in 10s");
		break;
	default:
		break;
	}
}

////////////////////////////////////////////////////////////////////////
// 네트워크 콜백
////////////////////////////////////////////////////////////////////////
void Dummy::OnConnected(uint32_t now)
{
	game = GameState::Entering;
	m_enterDeadline = now + ENTER_TIMEOUT_MS;

	std::string name = Name();
	lastKick = 0;
	PacketWriter w(PT_CS_ENTER_GAME);
	w.W32(GAME_PROTOCOL_VERSION);
	w.WName(name);
	Send(w);
}

std::string Dummy::Name() const
{
	std::string name = g_cfg.namePrefix + std::to_string(index + 1);
	if (name.size() > (size_t)NAME_LEN) name = name.substr(name.size() - NAME_LEN);
	return name;
}

const char* Dummy::StateName(GameState g) const
{
	switch (g)
	{
	case GameState::Entering: return "Entering";
	case GameState::InGame: return "InGame";
	case GameState::Dead: return "Dead";
	default: return "None";
	}
}

void Dummy::LogConnectFail(const char* reason, int err)
{
	g_eventLog.Add(EventLog::CONNECT_FAIL);
	g_eventLog.Write("CONNECT_FAIL", "#%d %s %s (%d %s)", index + 1, Name().c_str(), reason, err, EventLog::ErrorText(err).c_str());
}

void Dummy::OnClosed(uint32_t now, bool intended)
{
	if (!intended && game == GameState::InGame) g_stats.unexpectedDisconnects++;
	GameState prev = game;

	// 비정상 종료 기록: 우리가 끊지 않았고, 사망 후 서버의 정상 종료(death_disconnect)도 아닌 경우
	if (!intended && prev != GameState::Dead)
	{
		const char* reason = closeReason ? closeReason : "unknown";
		std::string errText = closeError ? std::to_string(closeError) + " " + EventLog::ErrorText(closeError) : std::string("-");
		if (closedWhileConnecting)
		{
			g_eventLog.Add(EventLog::CONNECT_FAIL);
			g_eventLog.Write("CONNECT_FAIL", "#%d %s %s (%s)", index + 1, Name().c_str(), reason, errText.c_str());
		}
		else
		{
			bool enterFail = (prev == GameState::Entering || prev == GameState::None);
			g_eventLog.Add(enterFail ? EventLog::ENTER_FAIL : EventLog::DISCONNECT);
			g_eventLog.Write(enterFail ? "ENTER_FAIL" : "DISCONNECT",
				"#%d %s id=%u state=%s reason=\"%s\" err=(%s) kick=%d connected=%dms lastRecv=%dms pos=(%.1f,%.1f) visible=%d sendQ=%dB",
				index + 1, Name().c_str(), m_myId, StateName(prev), reason, errText.c_str(), (int)lastKick,
				TimeDiff(now, connectedAt), TimeDiff(now, lastRecvAt), m_x, m_z, (int)m_remotes.size(), (int)sendQ.size());
		}
	}
	game = GameState::None;
	m_remotes.clear();
	m_hits.clear();
	m_targetId = 0;
	m_vx = m_vz = 0;

	// 다음 접속 시각: 사망 후엔 reconnect_delay, 그 외(끊김·실패)는 1~3초 백오프
	uint32_t delay = (prev == GameState::Dead) ? (uint32_t)g_cfg.reconnectDelayMs : 1000u;
	uint32_t at = now + delay + (uint32_t)RandInt(0, 1000);
	if (TimeDiff(at, m_reconnectAt) > 0) m_reconnectAt = at;
}

void Dummy::Die(uint32_t now)
{
	if (game == GameState::Dead) return;
	game = GameState::Dead;
	m_deadAt = now;
	g_stats.deaths++;
	if (!g_reconnectMode.load()) m_retired = true;
	m_vx = m_vz = 0;
	m_remotes.clear();
	m_hits.clear();
	m_targetId = 0;
}

void Dummy::OnPacket(const uint8_t* payload, int len, uint32_t now)
{
	PacketReader r(payload, len);
	uint16_t type = r.U16();

	switch (type)
	{
	case PT_SC_ENTER_GAME:
	{
		uint8_t result = r.U8();
		if (result != ENTER_OK)
		{
			if (result == ENTER_SERVER_FULL) { g_stats.enterFull++; m_reconnectAt = now + 5000 + (uint32_t)RandInt(0, 2000); }
			else g_stats.enterOther++;
			static const char* names[] = { "OK", "enter rejected: SERVER_FULL", "enter rejected: VERSION_MISMATCH", "enter rejected: INVALID_NAME" };
			g_net.Close(this, false, now, result < 4 ? names[result] : "enter rejected: unknown result");
			return;
		}
		m_myId = r.U32();
		r.U8();                         // room
		m_x = r.F(); m_z = r.F();
		r.U16(); r.U16();               // hp, maxHp
		m_moveSpeed = r.F();
		m_radius = r.F();
		m_weaponId = r.U8();
		uint32_t serverTime = r.U32();
		r.Skip(NAME_LEN * 2);
		r.U32();                        // map hash
		if (r.Remain() >= 4) m_sprintMul = r.F();

		game = GameState::InGame;
		g_stats.enterOk++;
		m_clockOffset = (int32_t)(serverTime - now);    // 대략값 → 핑으로 보정
		m_bestRtt = 1 << 30;
		m_pingsSent = 0;
		m_nextPing = now;
		m_nextHeartbeat = now + HEARTBEAT_MS;
		m_lastTick = now;
		m_nextTurn = now;
		m_nextTargetScan = now + 300;
		m_nextFire = now;
		m_vx = m_vz = 0;
		m_lastSentVx = m_lastSentVz = 0;
		m_forceMove = true;
		m_remotes.clear();
		m_hits.clear();
		m_targetId = 0;
		m_haveWeapon = false;
		m_rolling = false;
		m_rollReadyAt = now + 1000;
		m_nextWanderRollCheck = now + 1000;
		return;
	}
	case PT_SC_WEAPON_DEFS:
	{
		int n = r.U8();
		for (int i = 0; i < n && r.Ok(); i++)
		{
			WeaponInfo w;
			w.id = r.U8(); w.damage = r.U16(); w.range = r.F(); w.speed = r.F();
			w.intervalMs = r.U16(); w.radius = r.F();
			r.U16(); r.U16(); r.F(); r.U8(); r.U8();   // magazine, reload, spread, pellets, pierce
			r.F(); r.U16(); r.U8(); r.Skip(2);         // v6: jitterDeg, durability, slot, reserved (36B)
			if (r.Ok() && w.id == m_weaponId && w.speed > 0) { m_weapon = w; m_haveWeapon = true; }
		}
		return;
	}
	case PT_SC_CREATE_CHARACTERS:
	{
		int n = r.U8();
		for (int i = 0; i < n && r.Ok(); i++)
		{
			uint32_t id = r.U32();
			r.Skip(NAME_LEN * 2);
			RemotePlayer p;
			p.x = r.F(); p.z = r.F(); p.vx = r.F(); p.vz = r.F();
			r.F(); r.U16(); r.U16(); r.U8(); r.U8();   // aim, hp, maxHp, weapon, reserved
			p.t = now;
			if (r.Ok() && id != m_myId) m_remotes[id] = p;
		}
		return;
	}
	case PT_SC_DELETE_CHARACTERS:
	{
		int n = r.U8();
		for (int i = 0; i < n && r.Ok(); i++)
		{
			uint32_t id = r.U32();
			m_remotes.erase(id);
			if (id == m_targetId) m_targetId = 0;
		}
		return;
	}
	case PT_SC_ROLL:
	{
		// 대상이 굴렀다: 도착점으로 옮기고, 그 대상에 대한 미보고 명중은 버린다 (되감기 검증에서 빗나감 → 부정 카운트 방지)
		uint32_t id = r.U32();
		r.F(); r.F();
		RemotePlayer p;
		p.x = r.F(); p.z = r.F(); p.vx = 0; p.vz = 0;
		p.t = now + 250;
		if (!r.Ok() || id == m_myId) return;
		m_remotes[id] = p;
		m_hits.erase(std::remove_if(m_hits.begin(), m_hits.end(), [id](const PendingHit& h) { return h.targetId == id; }), m_hits.end());
		return;
	}
	case PT_SC_MOVE:
	{
		uint32_t id = r.U32();
		RemotePlayer p;
		p.x = r.F(); p.z = r.F(); p.vx = r.F(); p.vz = r.F();
		p.t = now;
		if (r.Ok() && id != m_myId) m_remotes[id] = p;
		return;
	}
	case PT_SC_POSITION_CORRECT:
	{
		m_x = r.F(); m_z = r.F();
		m_vx = m_vz = 0;
		m_rolling = false;
		m_nextTurn = now;
		m_forceMove = true;
		g_stats.corrections++;
		return;
	}
	case PT_SC_DAMAGE:
	{
		uint32_t attacker = r.U32();
		r.U32();                // victim
		if (attacker == m_myId) g_stats.hitsConfirmed++;
		return;
	}
	case PT_SC_PLAYER_DIE:
	{
		uint32_t victim = r.U32();
		uint32_t killer = r.U32();
		if (victim == m_myId) { Die(now); return; }
		if (killer == m_myId) g_stats.kills++;
		m_remotes.erase(victim);
		if (victim == m_targetId) m_targetId = 0;
		return;
	}
	case PT_SC_DEATH_RESULT:
		Die(now);
		return;
	case PT_SC_KICK:
	{
		uint8_t reason = r.U8();
		lastKick = reason;
		g_stats.kicks[reason < 5 ? reason : 0]++;
		return;
	}
	case PT_SC_PONG:
	{
		uint32_t clientTime = r.U32();
		uint32_t serverTime = r.U32();
		int rtt = TimeDiff(now, clientTime);
		if (rtt < 0) return;
		g_stats.AddRtt(rtt);
		// 최근 10초 안에서 RTT가 가장 작은 표본으로 오프셋 추정
		if (rtt <= m_bestRtt || TimeDiff(now, m_bestRttAt) > 10000)
		{
			m_bestRtt = rtt;
			m_bestRttAt = now;
			m_clockOffset = (int32_t)(serverTime + (uint32_t)(rtt / 2) - now);
		}
		return;
	}
	default:
		return;     // SC_FIRE, SC_SCORE, SC_RANKING_TOP3 등은 무시
	}
}

////////////////////////////////////////////////////////////////////////
// 게임 AI: 배회 → 적 발견 시 정지 후 사격
////////////////////////////////////////////////////////////////////////
void Dummy::GameTick(uint32_t now)
{
	float dt = TimeDiff(now, m_lastTick) / 1000.0f;
	if (dt > 0.2f) dt = 0.2f;
	if (dt < 0) dt = 0;
	m_lastTick = now;

	// 시각 동기화: 입장 직후 200ms 간격 3회, 이후 2초마다
	if (TimeDiff(now, m_nextPing) >= 0)
	{
		PacketWriter w(PT_CS_PING);
		w.W32(now);
		Send(w);
		m_pingsSent++;
		m_nextPing = now + (m_pingsSent < 3 ? 200 : PING_MS);
	}
	if (TimeDiff(now, m_nextHeartbeat) >= 0)
	{
		PacketWriter w(PT_CS_HEARTBEAT);
		Send(w);
		m_nextHeartbeat = now + HEARTBEAT_MS;
	}

	FlushHits(now);

	// 구르는 중: 위치만 보간, 이동 패킷·사격 없음
	if (UpdateRoll(now)) return;

	bool canFire = g_fireEnabled.load(std::memory_order_relaxed) && m_haveWeapon;
	if (canFire && TimeDiff(now, m_nextTargetScan) >= 0)
	{
		SelectTarget(now);
		m_nextTargetScan = now + 250 + (uint32_t)RandInt(0, 100);
		// 반응 속도: 새 대상을 잡으면 첫 사격까지 reaction_base_ms × 약함 만큼 더 기다린다
		if (m_targetId != m_lastTargetId)
		{
			m_lastTargetId = m_targetId;
			if (m_targetId) m_firstShotAt = now + (uint32_t)(g_cfg.reactionBaseMs * m_weak);
		}
	}

	auto it = (canFire && m_targetId) ? m_remotes.find(m_targetId) : m_remotes.end();
	if (it != m_remotes.end())
	{
		// 교전: 대상 주위를 옆으로 돌며 조준·사격 (game-spec 21.1)
		const RemotePlayer& t = it->second;
		float age = std::min(TimeDiff(now, t.t), REMOTE_EXTRAPOLATE_MAX_MS) / 1000.0f;
		float tx = t.x + t.vx * age, tz = t.z + t.vz * age;
		float dx = tx - m_x, dz = tz - m_z;
		float dist = sqrtf(dx * dx + dz * dz);
		float range = std::min(g_cfg.engageRange, m_weapon.range - 2.0f);
		if (dist > range || dist < 0.01f)
		{
			m_targetId = 0;
		}
		else
		{
			m_aim = ToAim(dx, dz);
			CombatMove(now, dt, dx, dz, dist, range);
			// 교전 중 구르기: 쿨타임이 지나면 초당 roll_combat_per_sec 확률로 적의 옆 방향
			if (g_cfg.roll && TimeDiff(now, m_rollReadyAt) >= 0 && Rand01() < g_cfg.rollCombatPerSec * dt)
			{
				float side = Rand01() < 0.5f ? 1.0f : -1.0f;
				if (TryRoll(now, -dz / dist * side, dx / dist * side)) return;
			}
			SendMoveIfNeeded(now);      // 위치·조준을 먼저 알린 뒤 사격 (서버 원점 검사: 3m 이내)
			// 대상이 구르는 중이면(SC_ROLL 후 0.25초) 쏘지 않는다: 되감기 위치가 경로 중간이라 명중 보고가 거부됨
			if (TimeDiff(now, m_nextFire) >= 0 && TimeDiff(now, t.t) >= 0 && TimeDiff(now, m_firstShotAt) >= 0)
				Fire(now, tx, tz, t.vx, t.vz);
			return;
		}
	}
	else
	{
		m_targetId = 0;
	}

	Wander(now, dt);
	// 배회 중 구르기: 쿨타임마다 roll_wander_chance 확률로 진행 방향
	if (g_cfg.roll && (m_vx != 0 || m_vz != 0) && TimeDiff(now, m_rollReadyAt) >= 0 && TimeDiff(now, m_nextWanderRollCheck) >= 0)
	{
		m_nextWanderRollCheck = now + (uint32_t)g_cfg.rollCooldownMs;
		if (Rand01() < g_cfg.rollWanderChance)
		{
			float sp = sqrtf(m_vx * m_vx + m_vz * m_vz);
			SendMoveIfNeeded(now);
			if (TryRoll(now, m_vx / sp, m_vz / sp)) return;
		}
	}
	SendMoveIfNeeded(now);
}

// 서버 HandleRoll과 같은 계산: 0.25m 단위 직진, 엄폐물(반지름 그대로)에 닿으면 그 앞에서 멈춤
bool Dummy::TryRoll(uint32_t now, float dx, float dz)
{
	float len = sqrtf(dx * dx + dz * dz);
	if (len < 0.5f) return false;
	dx /= len; dz /= len;
	float dist = m_moveSpeed * g_cfg.rollSpeedMult * (float)g_cfg.rollMs / 1000.0f;
	int steps = std::max(1, (int)ceilf(dist / 0.25f));
	float stepLen = dist / (float)steps;
	float cx = m_x, cz = m_z;
	for (int i = 0; i < steps; i++)
	{
		float nx = std::clamp(cx + dx * stepLen, MapConst::MinPos, MapConst::MaxPos);
		float nz = std::clamp(cz + dz * stepLen, MapConst::MinPos, MapConst::MaxPos);
		if (g_map.CircleBlocked(nx, nz, m_radius)) break;
		cx = nx; cz = nz;
	}
	if ((cx - m_x) * (cx - m_x) + (cz - m_z) * (cz - m_z) < 1.0f) return false;     // 벽 바로 앞 → 굴러도 의미 없음

	PacketWriter w(PT_CS_ROLL);
	w.WF(m_x); w.WF(m_z); w.WF(dx); w.WF(dz);
	Send(w);
	g_stats.rolls++;
	m_rolling = true;
	m_rollStart = now;
	m_rollReadyAt = now + (uint32_t)g_cfg.rollCooldownMs + 100;     // 서버 도착 편차 여유
	m_rollFromX = m_x; m_rollFromZ = m_z;
	m_rollToX = cx; m_rollToZ = cz;
	m_aim = ToAim(dx, dz);
	return true;
}

// 구르는 중이면 위치 보간 후 true. 끝나면 도착점에서 이동 패킷을 다시 보내도록 표시
bool Dummy::UpdateRoll(uint32_t now)
{
	if (!m_rolling) return false;
	float k = (float)TimeDiff(now, m_rollStart) / (float)g_cfg.rollMs;
	if (k >= 1.0f)
	{
		m_rolling = false;
		m_x = m_rollToX; m_z = m_rollToZ;
		m_forceMove = true;
		return false;
	}
	if (k < 0) k = 0;
	m_x = m_rollFromX + (m_rollToX - m_rollFromX) * k;
	m_z = m_rollFromZ + (m_rollToZ - m_rollFromZ) * k;
	return true;
}

void Dummy::SelectTarget(uint32_t now)
{
	float range = std::min(g_cfg.engageRange, m_weapon.range - 2.0f);
	float r2 = range * range;

	// 가까운 후보 8명까지 모아서, 가까운 순으로 시야선(벽) 검사
	struct Cand { float d2; uint32_t id; float x, z; };
	Cand best[8];
	int nBest = 0;
	for (const auto& kv : m_remotes)
	{
		const RemotePlayer& p = kv.second;
		float age = std::min(TimeDiff(now, p.t), REMOTE_EXTRAPOLATE_MAX_MS) / 1000.0f;
		float x = p.x + p.vx * age, z = p.z + p.vz * age;
		float dx = x - m_x, dz = z - m_z;
		float d2 = dx * dx + dz * dz;
		if (d2 > r2) continue;
		if (nBest < 8) best[nBest++] = { d2, kv.first, x, z };
		else
		{
			int worst = 0;
			for (int i = 1; i < 8; i++) if (best[i].d2 > best[worst].d2) worst = i;
			if (d2 < best[worst].d2) best[worst] = { d2, kv.first, x, z };
		}
	}
	std::sort(best, best + nBest, [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });

	m_targetId = 0;
	int checks = std::min(nBest, MAX_LOS_CHECKS);
	for (int i = 0; i < checks; i++)
	{
		if (!g_map.SegmentBlocked(m_x, m_z, best[i].x, best[i].z, true))
		{
			m_targetId = best[i].id;
			return;
		}
	}
}

void Dummy::Fire(uint32_t now, float tx, float tz, float tvx, float tvz)
{
	// 조준점: 확률 (1 - 약함)로 리드(탄 도착 시점의 대상 위치 예측), 아니면 현재 위치 (game-spec 20.5)
	float dx = tx - m_x, dz = tz - m_z;
	float flight = sqrtf(dx * dx + dz * dz) / m_weapon.speed;
	float ax = tx, az = tz;
	if (Rand01() >= m_weak) { ax = tx + tvx * flight; az = tz + tvz * flight; }
	dx = ax - m_x; dz = az - m_z;
	float dist = sqrtf(dx * dx + dz * dz);
	if (dist < 0.01f || dist > m_weapon.range) { m_targetId = 0; return; }
	if (g_map.SegmentBlocked(m_x, m_z, ax, az, true)) { m_targetId = 0; return; }     // 벽에 막힘

	// 조준 오차 ±aim_error_max_deg × 약함
	float err = (Rand01() * 2.0f - 1.0f) * g_cfg.aimErrorMaxDeg * m_weak * PI_F / 180.0f;
	float ux = dx / dist, uz = dz / dist;
	float c = cosf(err), sn = sinf(err);
	float fx = ux * c - uz * sn, fz = ux * sn + uz * c;

	// ViewTime: 내가 알고 있는 대상 위치에 해당하는 서버 시각 (= 추정 서버 시각 - 편도 지연)
	int oneWay = m_bestRtt < (1 << 29) ? m_bestRtt / 2 : 0;
	uint32_t viewTime = ServerNow(now) - (uint32_t)oneWay - 20;

	m_shotSeq++;
	PacketWriter w(PT_CS_FIRE);
	w.W32(m_shotSeq);
	w.W8(m_weapon.id);
	w.WF(m_x); w.WF(m_z);
	w.WF(fx); w.WF(fz);
	w.W32(viewTime);
	w.W16(0);
	Send(w);
	g_stats.shots++;

	// 연사 간격 × 1.1(서버 토큰 버킷 여유) × (1 + 약함)
	m_nextFire = now + (uint32_t)(m_weapon.intervalMs * 1.1f * (1.0f + m_weak)) + (uint32_t)RandInt(0, 20);

	// 실제 탄 경로로 명중 판정: 탄 도착 시점의 대상 예상 위치 P와 발사 직선의 최근접점
	float px = tx + tvx * flight, pz = tz + tvz * flight;
	float s = (px - m_x) * fx + (pz - m_z) * fz;
	if (s <= 0 || s > m_weapon.range) return;
	float cx = m_x + fx * s, cz = m_z + fz * s;
	float ex = px - cx, ez = pz - cz;
	float hitR = m_radius + m_weapon.radius;
	if (ex * ex + ez * ez > hitR * hitR) return;                 // 빗나감 → 보고하지 않음
	if (g_map.SegmentBlocked(m_x, m_z, cx, cz, true)) return;

	PendingHit h;
	h.due = now + (uint32_t)(s / m_weapon.speed * 1000.0f) + 20;
	h.shotSeq = m_shotSeq;
	h.targetId = m_targetId;
	h.hx = cx; h.hz = cz;
	m_hits.push_back(h);
}

void Dummy::FlushHits(uint32_t now)
{
	if (m_hits.empty()) return;
	PacketWriter w(PT_CS_HIT_REPORT);
	uint8_t count = 0;
	char items[MAX_HIT_ITEMS * LEN_HIT_ITEM];
	size_t keep = 0;
	for (size_t i = 0; i < m_hits.size(); i++)
	{
		const PendingHit& h = m_hits[i];
		if (TimeDiff(now, h.due) < 0) { m_hits[keep++] = h; continue; }
		if (TimeDiff(now, h.due) > HIT_STALE_MS) continue;
		if (m_remotes.find(h.targetId) == m_remotes.end()) continue;     // 이미 사망/시야 이탈
		if (count >= MAX_HIT_ITEMS) { m_hits[keep++] = h; continue; }
		char* p = items + count * LEN_HIT_ITEM;
		uint8_t pellet = 0;
		memcpy(p, &h.shotSeq, 4); memcpy(p + 4, &pellet, 1); memcpy(p + 5, &h.targetId, 4);
		memcpy(p + 9, &h.hx, 4); memcpy(p + 13, &h.hz, 4);
		count++;
	}
	m_hits.resize(keep);
	if (count == 0) return;

	w.W8(count);
	for (int i = 0; i < count * LEN_HIT_ITEM; i++) w.W8((uint8_t)items[i]);
	Send(w);
	g_stats.hitsReported += count;
}

void Dummy::PickHeading(uint32_t now)
{
	float speed = m_moveSpeed * (Rand01() < g_cfg.sprintChance ? m_sprintMul : 1.0f);
	for (int tryN = 0; tryN < 8; tryN++)
	{
		float a = Rand01() * 2.0f * PI_F;
		float cx = cosf(a), cz = sinf(a);
		float ax = m_x + cx * 3.0f, az = m_z + cz * 3.0f;
		if (ax < MapConst::MinPos + m_radius || az < MapConst::MinPos + m_radius ||
			ax > MapConst::MaxPos - m_radius || az > MapConst::MaxPos - m_radius) continue;
		if (g_map.CircleBlocked(ax, az, m_radius) || g_map.SegmentBlocked(m_x, m_z, ax, az, false)) continue;
		m_vx = cx * speed;
		m_vz = cz * speed;
		m_aim = ToAim(cx, cz);
		m_nextTurn = now + (uint32_t)RandInt(2000, 6000);
		return;
	}
	// 갈 곳이 없으면 잠시 정지 후 재시도
	m_vx = m_vz = 0;
	m_nextTurn = now + 500;
}

// 0.25m 단위로 나눠 이동 (클라 LocalPlayerController와 같은 방식). 막히면 그 앞에서 멈추고 false
bool Dummy::MoveStep(float dt)
{
	float mx = m_vx * dt, mz = m_vz * dt;
	float len = sqrtf(mx * mx + mz * mz);
	if (len <= 0) return true;
	int steps = std::max(1, (int)ceilf(len / 0.25f));
	float sx = mx / steps, sz = mz / steps;
	for (int i = 0; i < steps; i++)
	{
		float nx = m_x + sx, nz = m_z + sz;
		if (nx < MapConst::MinPos + m_radius || nz < MapConst::MinPos + m_radius ||
			nx > MapConst::MaxPos - m_radius || nz > MapConst::MaxPos - m_radius ||
			g_map.CircleBlocked(nx, nz, m_radius))
			return false;
		m_x = nx;
		m_z = nz;
	}
	return true;
}

void Dummy::Wander(uint32_t now, float dt)
{
	if ((m_vx == 0 && m_vz == 0) || TimeDiff(now, m_nextTurn) >= 0)
	{
		PickHeading(now);
		if (m_vx == 0 && m_vz == 0) return;
	}
	if (!MoveStep(dt))
	{
		// 막히면 멈추고 다음 틱에 방향 전환
		m_vx = m_vz = 0;
		m_nextTurn = now;
		m_forceMove = true;
	}
}

// 교전 중 이동: 대상의 옆 방향(1~3초마다 좌우 전환)으로 걷고, 너무 가까우면 뒤로 / 멀면 앞으로 (game-spec 21.1)
void Dummy::CombatMove(uint32_t now, float dt, float dx, float dz, float dist, float range)
{
	if (TimeDiff(now, m_nextStrafeSwitch) >= 0)
	{
		m_strafeSide = Rand01() < 0.5f ? 1.0f : -1.0f;
		m_nextStrafeSwitch = now + (uint32_t)RandInt(1000, 3000);
	}
	float ux = dx / dist, uz = dz / dist;
	float mx = -uz * m_strafeSide, mz = ux * m_strafeSide;
	if (dist < range * 0.4f) { mx -= ux * 0.8f; mz -= uz * 0.8f; }
	else if (dist > range - 4.0f) { mx += ux * 0.8f; mz += uz * 0.8f; }
	float ml = sqrtf(mx * mx + mz * mz);
	if (ml < 0.01f) { m_vx = m_vz = 0; return; }
	m_vx = mx / ml * m_moveSpeed;
	m_vz = mz / ml * m_moveSpeed;
	if (!MoveStep(dt))
	{
		// 엄폐물에 막힘 → 반대쪽으로
		m_strafeSide = -m_strafeSide;
		m_nextStrafeSwitch = now + (uint32_t)RandInt(1000, 3000);
		m_vx = m_vz = 0;
		m_forceMove = true;
	}
}

void Dummy::SendMoveIfNeeded(uint32_t now)
{
	bool velChanged = fabsf(m_vx - m_lastSentVx) > 0.01f || fabsf(m_vz - m_lastSentVz) > 0.01f;
	bool moving = m_vx != 0 || m_vz != 0;
	bool aimChanged = AngleDiff(m_aim, m_lastSentAim) >= AIM_SEND_DEG;
	int since = TimeDiff(now, m_lastMoveSend);

	bool send = false;
	if ((velChanged || m_forceMove) && since >= MOVE_MIN_MS) send = true;
	else if ((moving || aimChanged) && since >= MOVE_SEND_MS) send = true;
	if (!send) return;

	m_moveSeq++;
	PacketWriter w(PT_CS_MOVE);
	w.WF(m_x); w.WF(m_z); w.WF(m_vx); w.WF(m_vz); w.WF(m_aim);
	w.W16(m_moveSeq);
	Send(w);
	m_lastMoveSend = now;
	m_lastSentVx = m_vx;
	m_lastSentVz = m_vz;
	m_lastSentAim = m_aim;
	m_forceMove = false;
}
