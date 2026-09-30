#pragma once
#include "NetLib/NetLib_Content.h"
#include "Player.h"
#include "Define.h"
#include "CommonProtocol.h"
#include "GameProtocol.h"

class EchoContent : public NetLib_Content
{
public:
	inline static long sEchoSessionCount;

	int fps_cumulative;
	int fps_last;
	float cumulate = 0;
	float runtime = 0;
	DWORD currentTime;
	DWORD lastFpsTime;
	std::unordered_map<unsigned long long, Player*> players_sessionHandle;
	EchoContent(int frequency);
	virtual void OnBegin();
	virtual void OnEnd();
	virtual void OnError(int errcode);
	virtual void OnUpdate(float deltaTime);
	//virtual void OnRecv(unsigned long long sessionHandle, Packet* packet);
	virtual void OnRecv(unsigned long long sessionHandle, char* payload);
	virtual void OnEnter(unsigned long long sessionHandle, void* completionKey);
	virtual void OnLeave(unsigned long long sessionHandle);
	virtual void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port);
	void Timeout(DWORD currentTime);

	struct EchoContentMSG
	{
		unsigned long long sessionHandle;
		INT64 accountNo;
		LONGLONG sendtick;

		//Packet* sendpacket;
	};
	//RingBuffer squeue{10'000'000};
	LockFreeQueue<EchoContentMSG> sendqueue;
	static unsigned int __stdcall SendThread(void* params);
};

