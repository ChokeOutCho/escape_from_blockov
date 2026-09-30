#pragma once
#include "Utils/TLSObjectPool.h"
class WaitingSession
{
public:
	unsigned long acceptTime;
	unsigned long IP;
	unsigned short port;

	inline static TLSObjectPool<WaitingSession> pool{ 128 };

	__inline static WaitingSession* Alloc()
	{
		return pool.Alloc();
	}

	__inline static void Free(WaitingSession* waitingSession)
	{
		pool.Free(waitingSession);
		return;
	}

	__inline static int GetPoolUseSize()
	{
		return pool.GetUseSize();
	}

};