#include "EchoContent.h"
#include "ContentServer.h"
////////////////////////////////////////////////////////////////////////
/// 컨텐츠
////////////////////////////////////////////////////////////////////////
EchoContent::EchoContent(int tick)
	:NetLib_Content(tick)
{
	fps_cumulative = 0;
	fps_last = 0;
}

void EchoContent::OnBegin()
{
	//_beginthreadex(0, 0, SendThread, this, 0, 0);
	//_beginthreadex(0, 0, SendThread, this, 0, 0);
	currentTime = timeGetTime();
	lastFpsTime = timeGetTime();
}

void EchoContent::OnEnd()
{

}

void EchoContent::OnError(int errcode)
{

}

void EchoContent::OnUpdate(float deltaTime)
{
	fps_cumulative++;

	DWORD now = timeGetTime();
	if (now - lastFpsTime >= 1000) // 1초 경과
	{
		fps_last = fps_cumulative;   // 지난 1초 동안 프레임 수
		fps_cumulative = 0;
		lastFpsTime = now;

		Timeout(now);
		currentTime = now;
	}
}
void EchoContent::OnRecv(unsigned long long sessionHandle, char* payload)
{
	auto it = players_sessionHandle.find(sessionHandle);
	if (it == players_sessionHandle.end())
		return;
	Player* player = it->second;
	if (currentTime - player->lastRecv > 10000)
		player->lastRecv = currentTime;
	char* p = payload;
	WORD type = *(WORD*)p;
	p += sizeof(WORD);

	switch (type)
	{
	case en_PACKET_CS_GAME_REQ_ECHO:
	{
		INT64 accountNo = *(INT64*)p;
		p += sizeof(INT64);
		LONGLONG sendtick = *(LONGLONG*)p;

		Packet* sendpacket = Packet::NetAlloc();
		*sendpacket << (WORD)en_PACKET_CS_GAME_RES_ECHO << accountNo << sendtick;
		m_server->SendPacketFastWithoutIOCount(sessionHandle, sendpacket);

		break;
	}
	case en_PACKET_CS_GAME_REQ_HEARTBEAT:
	case CS_HEARTBEAT:
	{
		it->second->lastRecv = currentTime;
		break;
	}
	case CS_PING:
	{
		// 레이턴시 측정: 클라 시각을 그대로 돌려주고 서버 시각을 붙인다
		UINT32 clientTimeMs = *(UINT32*)p;
		Packet* sendpacket = Packet::NetAlloc();
		*sendpacket << (WORD)SC_PONG << (DWORD)clientTimeMs << (DWORD)GetServerTimeMs();
		m_server->SendPacketFastWithoutIOCount(sessionHandle, sendpacket);
		break;
	}

	default:
		m_server->Disconnect(sessionHandle);
		return;
	}
}


//void EchoContent::OnRecv(unsigned long long sessionHandle, Packet* packet)
//{
//	PROFILING("ONRECV");
//	Player* player;
//	auto itPlayers = players_sessionHandle.find(sessionHandle);
//	if (itPlayers != players_sessionHandle.end())
//	{
//		player = itPlayers->second;
//		player->lastRecv = timeGetTime();
//	}
//	else
//		return; // 어차피 안들어오는데 그냥 컴파일러 경고 보기 싫어서
//
//
//	WORD type;
//	*packet >> type;
//	switch (type)
//	{
//	case en_PACKET_CS_GAME_REQ_ECHO:
//	{
//		INT64 accountNo;
//		LONGLONG sendtick;
//		*packet >> accountNo >> sendtick;
//		if (accountNo != player->AccNo)
//		{
//			m_server->Disconnect(sessionHandle);
//			return;
//		}
//		
//		sendqueue.Enqueue({ sessionHandle, accountNo, sendtick });
//
//		//Packet* sendpacket = Packet::Alloc();
//		//*sendpacket << (WORD)en_PACKET_CS_GAME_RES_ECHO << accountNo << sendtick;
//		//{
//		//	PROFILING("SENDPACKET");
//		//	m_server->SendPacket(sessionHandle, sendpacket);
//		//}
//		//Packet::Free(sendpacket);
//
//		break;
//	}
//	case en_PACKET_CS_GAME_REQ_HEARTBEAT:
//	{
//
//		break;
//	}
//
//	default:
//		m_server->Disconnect(sessionHandle); // 재로그인도 거름
//		return;
//	}
//	//player->pingpongCnt++;
//	//if (player->pingpongCnt > 1)
//	//{
//	//	player->pingpongCnt = 0;
//	//	((ContentServer*)m_server)->Move_Echo_LoadBalance(sessionHandle, player);
//
//	//}
//}

void EchoContent::OnEnter(unsigned long long sessionHandle, void* completionKey)
{
	Player* player = (Player*)completionKey;
	players_sessionHandle[sessionHandle] = player;
	InterlockedIncrement(&sEchoSessionCount);
	Packet* sendpacket = Packet::Alloc();
	*sendpacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)1 << player->AccNo;
	m_server->SendPacket(sessionHandle, sendpacket);
	Packet::Free(sendpacket);
}

void EchoContent::OnLeave(unsigned long long sessionHandle)
{
	players_sessionHandle.erase(sessionHandle);
	InterlockedDecrement(&sEchoSessionCount);
}

void EchoContent::OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
{
	//ContentServer* server = (ContentServer*)m_server;
	unsigned long long accNo = 0;
	auto itPlayers = players_sessionHandle.find(sessionHandle);
	if (itPlayers != players_sessionHandle.end())
	{
		Player* player = itPlayers->second;
		players_sessionHandle.erase(itPlayers);

		AuthContent::sRequestQueue.Enqueue({ player });
	}
	InterlockedDecrement(&sEchoSessionCount);

}


void EchoContent::Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> timeoutSessions;

	for (const auto& [sessionHandle, player] : players_sessionHandle)
	{
		DWORD lastRecv = player->lastRecv;
		if (currentTime < lastRecv) continue;

		DWORD elapsed = currentTime - lastRecv;
		if (elapsed > HEARTBEAT_TIME_OUT)
		{
			LOG(L"TIMEOUT", LEVEL_DEBUG,
				L"Timeout session=%llu accNo=%lld elapsed=%u",
				sessionHandle, player->AccNo, elapsed);

			timeoutSessions.push_back(sessionHandle);
		}
	}

	for (auto session : timeoutSessions)
	{
		m_server->Disconnect(session);
	}

}