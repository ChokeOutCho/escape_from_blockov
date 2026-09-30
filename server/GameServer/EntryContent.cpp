#include "EntryContent.h"

EntryContent::EntryContent(const GameConfig& cfg, const std::vector<BattleContent*>& rooms)
	: NetLib_Content(cfg.entryTickMs), m_cfg(cfg), m_rooms(rooms), m_rng((unsigned)timeGetTime())
{
}

void EntryContent::OnBegin()
{
	m_lastCheck = GetServerTimeMs();
	GameLog("[entry] begin (rooms %d)", (int)m_rooms.size());
}

void EntryContent::OnEnter(unsigned long long sessionHandle, void* completionKey)
{
	m_waiting[sessionHandle] = { GetServerTimeMs(), false };
	m_waitingCount = (long)m_waiting.size();
}

void EntryContent::OnLeave(unsigned long long sessionHandle)
{
	// 방으로 이동 (Move_Content를 호출한 이 Content의 스레드에서 실행됨)
	m_waiting.erase(sessionHandle);
	m_waitingCount = (long)m_waiting.size();
}

void EntryContent::OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
{
	m_waiting.erase(sessionHandle);
	m_waitingCount = (long)m_waiting.size();
}

void EntryContent::OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen)
{
	auto it = m_waiting.find(sessionHandle);
	if (it == m_waiting.end() || it->second.closing) return;
	uint32_t now = GetServerTimeMs();

	PayloadReader r(payload, payloadLen);
	uint16_t type = r.U16();

	if (type == PT_CS_HEARTBEAT && payloadLen == LEN_CS_HEARTBEAT) return;
	if (type == PT_CS_PING && payloadLen == LEN_CS_PING)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_PONG);
		W32(pkt, r.U32());
		W32(pkt, GetServerTimeMs());
		m_server->SendPacketFastWithoutIOCount(sessionHandle, pkt);
		return;
	}
	if (type != PT_CS_ENTER_GAME || payloadLen != LEN_CS_ENTER_GAME)
	{
		Packet* pkt = Packet::NetAlloc();
		W16(pkt, PT_SC_KICK);
		W8(pkt, KICK_INVALID_PACKET);
		m_server->SendPacketFastWithoutIOCount(sessionHandle, pkt);
		it->second.closing = true;
		m_pending.push_back({ sessionHandle, now + 300 });
		return;
	}

	uint32_t version = r.U32();
	char16_t rawName[NAME_LEN];
	r.Name(rawName, NAME_LEN);

	if (version != GAME_PROTOCOL_VERSION)
	{
		Reject(sessionHandle, ENTER_VERSION_MISMATCH, now);
		return;
	}

	BattleContent* room = ReserveRoom();
	if (room == nullptr)
	{
		Reject(sessionHandle, ENTER_SERVER_FULL, now);
		return;
	}

	GamePlayer* player = new GamePlayer(sessionHandle);
	NormalizeName(rawName, player->name, m_rng);

	if (!m_server->Move_Content(room, sessionHandle, player))
	{
		// 세션이 이미 끊김
		room->Unreserve();
		delete player;
	}
}

BattleContent* EntryContent::ReserveRoom()
{
	// 채우기 우선: 방 번호 순으로 첫 번째 여유 방
	for (BattleContent* room : m_rooms)
		if (room->TryReserve()) return room;
	return nullptr;
}

void EntryContent::Reject(unsigned long long h, uint8_t result, uint32_t now)
{
	// Result != 0 이면 나머지 필드는 0 (고정 크기 유지)
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_ENTER_GAME);
	W8(pkt, result);
	for (int i = 0; i < LEN_SC_ENTER_GAME - 3; i++) W8(pkt, 0);
	m_server->SendPacketFastWithoutIOCount(h, pkt);

	auto it = m_waiting.find(h);
	if (it != m_waiting.end()) it->second.closing = true;
	m_pending.push_back({ h, now + 1000 });
	GameLog("[entry] reject session 0x%llx result %d", h, result);
}

void EntryContent::KickTimeout(unsigned long long h, uint32_t now)
{
	Packet* pkt = Packet::NetAlloc();
	W16(pkt, PT_SC_KICK);
	W8(pkt, KICK_TIMEOUT);
	m_server->SendPacketFastWithoutIOCount(h, pkt);
	m_pending.push_back({ h, now + 300 });
}

void EntryContent::OnUpdate(float deltaTime)
{
	uint32_t now = GetServerTimeMs();

	if (TimeDiff(now, m_lastCheck) >= 500)
	{
		m_lastCheck = now;
		for (auto& kv : m_waiting)
		{
			if (!kv.second.closing && TimeDiff(now, kv.second.enterTime) > m_cfg.enterTimeoutMs)
			{
				kv.second.closing = true;
				KickTimeout(kv.first, now);
			}
		}
	}

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
}

void EntryContent::NormalizeName(const char16_t* in, char16_t* out, std::mt19937& rng)
{
	char16_t tmp[NAME_LEN];
	int n = 0;
	for (int i = 0; i < NAME_LEN; i++)
	{
		char16_t c = in[i];
		if (c == 0) break;
		if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) continue;   // 제어문자 제거
		tmp[n++] = c;
	}
	auto isSpace = [](char16_t c) { return c == u' ' || c == u'\t' || c == 0x3000 || c == 0x00A0; };
	int b = 0, e = n;
	while (b < e && isSpace(tmp[b])) b++;
	while (e > b && isSpace(tmp[e - 1])) e--;
	// 잘린 서로게이트 쌍 제거
	if (e > b && tmp[e - 1] >= 0xD800 && tmp[e - 1] <= 0xDBFF) e--;

	for (int i = 0; i < NAME_LEN; i++) out[i] = 0;
	if (e <= b)
	{
		int num = std::uniform_int_distribution<int>(0, 9999)(rng);
		char buf[16];
		snprintf(buf, sizeof(buf), "Guest%04d", num);
		for (int i = 0; buf[i] && i < NAME_LEN; i++) out[i] = (char16_t)buf[i];
		return;
	}
	for (int i = b; i < e; i++) out[i - b] = tmp[i];
}
