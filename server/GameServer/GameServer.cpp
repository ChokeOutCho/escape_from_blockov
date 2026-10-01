#include "GameServer.h"

GameServer::GameServer(const GameConfig& cfg, const WeaponTable& weapons, const ObstacleMap& obstacles)
	: NetLib_Server(L"0.0.0.0", (unsigned short)cfg.port, cfg.workerThreads, cfg.concurrentThreads,
	                cfg.maxSessions, cfg.zeroCopy, nullptr /* 암호화 미사용 */, cfg.sendBuf),
	  m_cfg(cfg)
{
	for (int i = 0; i < cfg.roomCount; i++)
		m_rooms.push_back(new BattleContent(i, cfg, weapons, obstacles));
	m_entry = new EntryContent(cfg, m_rooms);

	RegistContent(m_entry);
	for (BattleContent* room : m_rooms)
		RegistContent(room);
}

void GameServer::OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port)
{
	Move_Content(m_entry, sessionHandle, nullptr);
}
