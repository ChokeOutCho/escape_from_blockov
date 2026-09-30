#include "ContentServer.h"

////////////////////////////////////////////////////////////////////////
/// 서버
////////////////////////////////////////////////////////////////////////
ContentServer::ContentServer(
	const WCHAR* openIP,
	unsigned short openPort,
	int opt_workerTH_Pool_size,
	int opt_concurrentTH_size,
	int opt_maxOfSession,
	bool opt_zerocpy,
	Opt_Encryption* opt_encryption,
	int opt_maxOfSendPackets)
	:NetLib_Server(openIP, openPort, opt_workerTH_Pool_size, opt_concurrentTH_size,
		opt_maxOfSession, opt_zerocpy, opt_encryption, opt_maxOfSendPackets)
{
	for (int i = 0; i < ECHO_CONTENT_SIZE; i++)
	{
		echoContents[i] = new EchoContent(34);
		RegistContent(echoContents[i]);
	}
	for (int i = 0; i < AUTH_CONTENT_SIZE; i++)
	{
		authContents[i] = new AuthContent(34);
		RegistContent(authContents[i]);
	}
	//_beginthreadex(0, 0, ContentThread, this, 0, 0);
}

void ContentServer::OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port)
{
	//InsertWaiting(IP, port, sessionHandle);
	WaitingSession* waitsession = WaitingSession::Alloc();
	waitsession->IP = IP;
	waitsession->port = port;
	Move_Auth_LoadBalance(sessionHandle, waitsession);
	//Move_Auth_LoadBalance(sessionHandle, nullptr);

}

void ContentServer::OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
{
	printf("여기오면안됨");
}

bool ContentServer::OnConnectionRequest(unsigned long IP, unsigned short port)
{
	return true;
}

void ContentServer::OnRecv(unsigned long long sessionHandle, Packet* packet)
{
	printf("여기오면안됨");
}

void ContentServer::Move_Echo_LoadBalance(unsigned long long sessionHandle, void* completionKey)
{
	NetLib_Content* selected = echoContents[0];
	for (int i = 0; i < ECHO_CONTENT_SIZE; i++)
	{
		if (selected->GetSessionCount() > echoContents[i]->GetSessionCount())
		{
			selected = echoContents[i];
		}
	}

	Move_Content(selected, sessionHandle, completionKey);
}

void ContentServer::Move_Auth_LoadBalance(unsigned long long sessionHandle, void* completionKey)
{
	NetLib_Content* selected = authContents[0];
	for (int i = 0; i < AUTH_CONTENT_SIZE; i++)
	{
		if (selected->GetSessionCount() > authContents[i]->GetSessionCount())
		{
			selected = authContents[i];
		}
	}
	Move_Content(selected, sessionHandle, completionKey);
}
void ContentServer::InsertBlacklist(unsigned long IP, unsigned short Port)
{
	if (use_blacklist == false) return;
	WCHAR IPPort[32];
	NetLib_Helper::IPPortToWstring(IP, Port, IPPort, 32);
	LOG(L"BlackList_Insert", LEVEL_DEBUG, L"Insert Blacklist. %s", IPPort);

	AcquireSRWLockExclusive(&lock_blacklist);
	blacklist[IP] = Port;
	ReleaseSRWLockExclusive(&lock_blacklist);
}

void ContentServer::ClearBlacklist()
{
	LOG(L"BlackList_Clear", LEVEL_DEBUG, L"Clearing entire blacklist.");

	AcquireSRWLockExclusive(&lock_blacklist);
	blacklist.clear();
	ReleaseSRWLockExclusive(&lock_blacklist);

}

void ContentServer::InsertWaiting(unsigned long IP, unsigned short Port, unsigned long long sessionHandle)
{
	WaitingSession* newsession = WaitingSession::Alloc();
	newsession->IP = IP;
	newsession->port = Port;
	newsession->acceptTime = timeGetTime();
	AcquireSRWLockExclusive(&lock_waiting);
	waiting[sessionHandle] = newsession;
	ReleaseSRWLockExclusive(&lock_waiting);
}
bool ContentServer::DeleteWaiting(unsigned long long sessionHandle)
{
	bool ret = false;
	AcquireSRWLockExclusive(&lock_waiting);
	auto it = waiting.find(sessionHandle);
	if (it != waiting.end())
		ret = true;
	ReleaseSRWLockExclusive(&lock_waiting);

	return ret;
}

void ContentServer::Login_Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> loginTimeoutSessions;

	AcquireSRWLockShared(&lock_waiting);
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
	ReleaseSRWLockShared(&lock_waiting);

	for (auto session : loginTimeoutSessions)
	{
		Disconnect(session);
		waiting.erase(session);
	}
}

unsigned int __stdcall ContentServer::ContentThread(void* params)
{
	ContentServer* server = (ContentServer*)params;
	while (1)
	{
		server->Login_Timeout(timeGetTime());
		Sleep(1000);
	}
}
