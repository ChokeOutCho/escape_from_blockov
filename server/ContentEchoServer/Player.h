#pragma once
#include "Utils/TLSObjectPool.h"
class Player
{
private:
	unsigned long long m_sessionHandle;

public:
	int pingpongCnt;
	DWORD lastRecv;
	unsigned long IP;
	unsigned short port;
	INT64 AccNo;

	unsigned long long GetSessionHandle() const
	{
		return m_sessionHandle;
	}

	static Player* CreatePlayer(unsigned long long sessionHandle, unsigned long long accNo)
	{
		Player* newPlayer = playerPool.Alloc();
		newPlayer->m_sessionHandle = sessionHandle;
		newPlayer->pingpongCnt = 0;
		newPlayer->lastRecv = timeGetTime();
		newPlayer->AccNo = accNo;
		return newPlayer;
	}

	static void DeletePlayer(Player* player)
	{
		playerPool.Free(player);
	}
	__inline static int PlayerPoolCurrentSize()
	{
		return playerPool.GetUseSize();
	}
private:
	friend class ObjectPool_TLSPoolComponent<Player>;
	inline static TLSObjectPool<Player> playerPool{ 128 };
	Player()
	{
	}
};