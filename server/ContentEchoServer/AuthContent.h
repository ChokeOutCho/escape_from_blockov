#pragma once
#include "NetLib/NetLib_Content.h"
#include "Player.h"
#include "WaitingSession.h"
#include "CommonProtocol.h"
#include "Define.h"
#include <mutex>
class ContentServer;

class AuthContent : public NetLib_Content
{
public:
	inline static std::mutex slock_loginPlayers[10];
	inline static std::unordered_map<long long, Player*> sloginPlayers[10];
	inline static long sAuthSessionCount;
	int fps_cumulative;
	int fps_last;
	DWORD lastFpsTime;
	std::mutex lock_sessions_accNo;
	std::unordered_map<unsigned long long, long long> sessions_accNo;
	std::unordered_map<unsigned long long, WaitingSession*> waiting;
	AuthContent(int frequency);
	float cumulate = 0;
	float runtime = 0;

	inline static LockFreeQueue<Player*> sRequestQueue;

	virtual void OnBegin();
	virtual void OnEnd();
	virtual void OnError(int errcode);
	virtual void OnUpdate(float deltaTime);
	//virtual void OnRecv(unsigned long long sessionHandle, Packet* packet);
	virtual void OnRecv(unsigned long long sessionHandle, char* payload);
	virtual void OnEnter(unsigned long long sessionHandle, void* completionKey);
	virtual void OnLeave(unsigned long long sessionHandle);
	virtual void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port);
	void Login_Timeout(DWORD currentTime);

	int GetShardingIndex(INT64 accNo);
};