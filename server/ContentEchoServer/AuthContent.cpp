#include "AuthContent.h"
#include "ContentServer.h"
////////////////////////////////////////////////////////////////////////
/// 컨텐츠
////////////////////////////////////////////////////////////////////////
AuthContent::AuthContent(int frequency)
	:NetLib_Content(frequency)
{
}

void AuthContent::OnBegin()
{
	printf("%llu 시작\n", GetID());
}

void AuthContent::OnEnd()
{

}

void AuthContent::OnError(int errcode)
{

}

void AuthContent::OnUpdate(float deltaTime)
{
	fps_cumulative++;

	DWORD now = timeGetTime();
	if (now - lastFpsTime >= 1000) // 1초 경과
	{
		fps_last = fps_cumulative;   // 지난 1초 동안 프레임 수
		fps_cumulative = 0;
		lastFpsTime = now;

		Login_Timeout(timeGetTime());
	}

	while (1)
	{
		Player* player;
		if (sRequestQueue.Dequeue(player) == -1)
			break;

		INT64 accNo = player->AccNo;
		int idx = GetShardingIndex(accNo);

		// 플레이어가 가지고있는 accNo가 동일한데 sessionhandle이 다르면 자료구조에서 빼선 안된다.
		// 단 플레이어는 항상 삭제한다.
		slock_loginPlayers[idx].lock();
		auto itLoginPlayer = sloginPlayers[idx].find(accNo);
		if (itLoginPlayer != sloginPlayers[idx].end())
		{
			if (itLoginPlayer->second->GetSessionHandle() == player->GetSessionHandle())
				sloginPlayers[idx].erase(itLoginPlayer);
		}
		slock_loginPlayers[idx].unlock();

		Player::DeletePlayer(player);
	}


}
void AuthContent::OnRecv(unsigned long long sessionHandle, char* payload)
{
	WORD type = *(WORD*)payload;
	payload += sizeof(WORD);
	switch (type)
	{
	case en_PACKET_CS_GAME_REQ_LOGIN:
	{
		INT64 AccountNo = *(INT64*)payload;
		payload += sizeof(INT64);
		char SessionKey[64];
		memcpy(SessionKey, payload, 64);
		payload += 64;
		int Version = *(int*)payload;

		unsigned char res_login = 0;
		unsigned long IP = 0;
		unsigned short port = 0;

		// 대기열에서 제거
		auto it_wait = waiting.find(sessionHandle);
		if (it_wait != waiting.end())
		{
			WaitingSession* waitsession = it_wait->second;
			IP = waitsession->IP;
			port = waitsession->port;
			waiting.erase(it_wait);
			WaitingSession::Free(waitsession);
		}
		else
		{
			m_server->Disconnect(sessionHandle); // 왜 없을까
			return;
		}

		int idx = GetShardingIndex(AccountNo);
		if (idx == -1)
		{
			m_server->Disconnect(sessionHandle);
			return;
		}

		// 중복 로그인 검사 후 플레이어 생성
		Player* player;
		slock_loginPlayers[idx].lock();
		auto it_acc = sloginPlayers[idx].find(AccountNo);
		if (it_acc != sloginPlayers[idx].end())
		{
			// 중복 로그인패킷 감지
			// 더미환경 특수 절차. 후에 요청한 로그인을 받는다.
			unsigned long long playerSessionHandle = it_acc->second->GetSessionHandle();
			player = Player::CreatePlayer(sessionHandle, AccountNo);
			(sloginPlayers[idx])[AccountNo] = player;
			player->AccNo = AccountNo;
			player->IP = IP;
			player->port = port;
			slock_loginPlayers[idx].unlock();

			sessions_accNo[sessionHandle] = AccountNo;
			m_server->Disconnect(playerSessionHandle);
			res_login = 1;
		}
		else
		{
			player = Player::CreatePlayer(sessionHandle, AccountNo);
			(sloginPlayers[idx])[AccountNo] = player;
			player->AccNo = AccountNo;
			player->IP = IP;
			player->port = port;
			slock_loginPlayers[idx].unlock();

			sessions_accNo[sessionHandle] = AccountNo;
			res_login = 1;
		}

		((ContentServer*)m_server)->Move_Echo_LoadBalance(sessionHandle, player);


		break;
	}
	default:
		m_server->Disconnect(sessionHandle);
		break;
	}


}

//void AuthContent::OnRecv(unsigned long long sessionHandle, Packet* packet)
//{
//	WORD type;
//	*packet >> type;
//	switch (type)
//	{
//	case en_PACKET_CS_GAME_REQ_LOGIN:
//	{
//		INT64 AccountNo;
//		char SessionKey[64];
//		int Version;
//		*packet >> AccountNo;
//		packet->GetData(SessionKey, 64);
//		*packet >> Version;
//
//		unsigned char res_login = 0;
//		unsigned long IP = 0;
//		unsigned short port = 0;
//
//		// 대기열에서 제거
//		auto it_wait = waiting.find(sessionHandle);
//		if (it_wait != waiting.end())
//		{
//			WaitingSession* waitsession = it_wait->second;
//			IP = waitsession->IP;
//			port = waitsession->port;
//			waiting.erase(it_wait);
//			WaitingSession::Free(waitsession);
//		}
//		else
//		{
//			m_server->Disconnect(sessionHandle);
//			return;
//		}
//
//		// 서버버전
//		//ContentServer* server = (ContentServer*)m_server;
//		//AcquireSRWLockExclusive(&server->lock_waiting);
//		//auto it = server->waiting.find(sessionHandle);
//		//if (it != server->waiting.end())
//		//{
//		//	WaitingSession* waitsession = it->second;
//		//	IP = waitsession->IP;
//		//	port = waitsession->port;
//		//	server->waiting.erase(it);
//		//	WaitingSession::Free(waitsession);
//		//}
//		//else
//		//{
//		//	m_server->Disconnect(sessionHandle);
//		//	return;
//		//}
//		//ReleaseSRWLockExclusive(&server->lock_waiting);
//
//
//		int idx = GetShardingIndex(AccountNo);
//		if (idx == -1)
//		{
//			m_server->Disconnect(sessionHandle);
//			return;
//		}
//
//		// 중복 로그인 검사 후 플레이어 생성
//		Player* player;
//		slock_loginPlayers[idx].lock();
//		auto it_acc = sloginPlayers[idx].find(AccountNo);
//		if (it_acc != sloginPlayers[idx].end())
//		{
//			// 중복 로그인패킷 감지
//			// 더미환경 특수 절차. 후에 요청한 로그인을 받는다.
//			unsigned long long playerSessionHandle = it_acc->second->GetSessionHandle();
//			player = Player::CreatePlayer(sessionHandle, AccountNo);
//			(sloginPlayers[idx])[AccountNo] = player;
//			player->AccNo = AccountNo;
//			player->IP = IP;
//			player->port = port;
//			slock_loginPlayers[idx].unlock();
//
//			sessions_accNo[sessionHandle] = AccountNo;
//			m_server->Disconnect(playerSessionHandle);
//			res_login = 1;
//		}
//		else
//		{
//			player = Player::CreatePlayer(sessionHandle, AccountNo);
//			(sloginPlayers[idx])[AccountNo] = player;
//			player->AccNo = AccountNo;
//			player->IP = IP;
//			player->port = port;
//			slock_loginPlayers[idx].unlock();
//
//			sessions_accNo[sessionHandle] = AccountNo;
//			res_login = 1;
//		}
//
//		((ContentServer*)m_server)->Move_Echo_LoadBalance(sessionHandle, player);
//
//
//		break;
//	}
//	default:
//		m_server->Disconnect(sessionHandle);
//		break;
//	}
//
//
//}

void AuthContent::OnEnter(unsigned long long sessionHandle, void* completionKey)
{
	WaitingSession* waitsession = (WaitingSession*)completionKey;
	waitsession->acceptTime = timeGetTime();
	waiting[sessionHandle] = waitsession;
	InterlockedIncrement(&sAuthSessionCount);

}

void AuthContent::OnLeave(unsigned long long sessionHandle)
{
	sessions_accNo.erase(sessionHandle);
	InterlockedDecrement(&sAuthSessionCount);

}

void AuthContent::OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
{
	
	auto itWaiting = waiting.find(sessionHandle);
	if (itWaiting != waiting.end())
	{
		WaitingSession* waitSession = itWaiting->second;
		waiting.erase(itWaiting);
		WaitingSession::Free(waitSession);
	}

	auto itSessions = sessions_accNo.find(sessionHandle);
	if (itSessions != sessions_accNo.end())
	{
		INT64 accNo = itSessions->second;
		int idx = GetShardingIndex(accNo);
		sessions_accNo.erase(sessionHandle);

		Player* player = nullptr;

		slock_loginPlayers[idx].lock();
		auto itLoginPlayers = sloginPlayers[idx].find(accNo);
		if (itLoginPlayers != sloginPlayers[idx].end())
		{
			player = itLoginPlayers->second;
			sloginPlayers[idx].erase(itLoginPlayers);
		}
		slock_loginPlayers[idx].unlock();

		if (player != nullptr)
			Player::DeletePlayer(player);
	}
	InterlockedDecrement(&sAuthSessionCount);

}

void AuthContent::Login_Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> loginTimeoutSessions;

	for (const auto& [sessionHandle, session] : waiting)
	{
		DWORD acceptTime = session->acceptTime;
		if (currentTime < acceptTime) continue;

		DWORD elapsed = currentTime - acceptTime;
		if (elapsed > LOGIN_TIME_OUT)
		{
			LOG(L"LOGIN_TIMEOUT", LEVEL_DEBUG,
				L"LoginTimeout session=%llu acceptTime=%u elapsed=%u",
				sessionHandle, acceptTime, elapsed);

			loginTimeoutSessions.push_back(sessionHandle);
		}
	}

	for (auto session : loginTimeoutSessions)
	{
		m_server->Disconnect(session);
		waiting.erase(session);
	}
}

int AuthContent::GetShardingIndex(INT64 accNo)
{
	if (accNo < 0 || accNo >100000)
	{
		return -1;
	}
	return accNo / 10000;
}