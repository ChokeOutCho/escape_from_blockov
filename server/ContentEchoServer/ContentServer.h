#pragma once
#include "NetLib/NetLib_Server.h";
#include "Define.h"
#include "Player.h"
#include "CommonProtocol.h"
#include "EchoContent.h"
#include "WaitingSession.h"
#include "AuthContent.h"

#define ECHO_CONTENT_SIZE 1
#define AUTH_CONTENT_SIZE 1
class ContentServer :public NetLib_Server
{
public:
	bool use_blacklist;
	EchoContent* echoContents[ECHO_CONTENT_SIZE];
	AuthContent* authContents[AUTH_CONTENT_SIZE];

	SRWLOCK lock_blacklist;
	std::unordered_map<unsigned long, unsigned short> blacklist;

	SRWLOCK lock_waiting;

	std::unordered_map<unsigned long long, WaitingSession*> waiting;

	ContentServer(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets);
	virtual void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port);
	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port);
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port);
	virtual void OnRecv(unsigned long long sessionHandle, Packet* packet);
	void Move_Echo_LoadBalance(unsigned long long sessionHandle, void* completionKey);
	void Move_Auth_LoadBalance(unsigned long long sessionHandle, void* completionKey);
	void InsertBlacklist(unsigned long IP, unsigned short Port);
	void ClearBlacklist();
	void InsertWaiting(unsigned long IP, unsigned short Port, unsigned long long sessionHandle);
	bool DeleteWaiting(unsigned long long sessionHandle);
	void Login_Timeout(DWORD currentTime);
	static unsigned int __stdcall ContentThread(void* params);

	long GetAuthFPS() { return authContents[0]->fps_last; }
	long GetEchoFPS() { return echoContents[0]->fps_last; }
};