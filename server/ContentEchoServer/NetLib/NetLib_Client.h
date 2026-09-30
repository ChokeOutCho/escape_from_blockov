#pragma once
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <map>
#include <stack>
#include <process.h>

#include "Session.h"
#include "../Utils/SimpleEncoder.h"
#include "../Utils/LockFreeStack.h"
#include "../Utils/ObjectPool.h"
#include "Packet.h"
#include "../Utils/Profiler.h"
#include <winsock2.h>
#include "ws2tcpip.h"
#include "NetLibDefine.h"
#include "../Utils/LockFreeQueue.h"

class NetLib_Client
{
public:
	NetLib_Client(const WCHAR* connectIP, unsigned short connectPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, bool opt_zerocpy, bool opt_use_monitor, Opt_Encryption* opt_encryption);
	virtual ~NetLib_Client();
	void Stop();

	bool Connect();

	__inline int GetPacketUseSize() { return Packet::GetPoolUseSize(); }

	bool SendPacket(Packet* packet);
	bool Disconnect();

	virtual void OnEnterJoinServer() {}
	virtual void OnLeaveServer() {}
	virtual void OnRecv(Packet* packet) {}
	virtual void OnSend(int sessionID) {}
	virtual void OnWorkerThreadBegin() {}
	virtual void OnWorkerThreadEnd() {}
	virtual void OnError(int errcode) {}

	/// <summary>
	/// 세션을 찾지 못했다면 nullptr반환
	/// </summary>
	/// <param name="sessionHandle"></param>
	/// <returns></returns>
	__inline Session* FindSession(unsigned long long sessionHandle);
	__inline bool		ReturnSession(Session* session) { return Decrement_IOCount(session); }
	int GetTPS_Recv() { return m_tps_recv; }
	int GetTPS_Send() { return m_tps_send; }

	int GetSessionTPS_Send(unsigned long long sessionHandle);
	int GetSessionTPS_Recv(unsigned long long sessionHandle);

	bool IsConnect() { return m_Connect; }
protected:

private:
	__inline void CreateSessionHandle(unsigned short index, unsigned long long sessionID, unsigned long long* outSessionHandle);
	__inline void DecodeSessionHandle(unsigned long long sessionHandle, unsigned short* outIndex, unsigned long long* outSessionID);

	/// <summary>
	///  Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	bool Increment_IOCount(Session* session);

	/// <summary>
	/// Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	bool Decrement_IOCount(Session* session);
	void ReleaseSession(Session* session);


	static unsigned int __stdcall WorkerThread(void* argv);
	static unsigned int __stdcall MonitorThread(void* argv);

	HANDLE m_iocpHandle;
	HANDLE m_monitorThread;
	HANDLE m_threadPool[4];
	Session m_server_session;
	SOCKET m_server_socket;
	SOCKADDR_IN m_server_addr;
	LockFreeQueue <TPS_SET> m_tpsSets;
	unsigned long long m_idPool;
	int m_errCode;
	bool m_Connect;

	int m_tps_recv;
	int m_tps_send;

	int m_opt_workerTH_count;
	int m_opt_concurrentTH_size;
	int m_opt_maxOfSession;
	bool m_opt_encryption;
	bool m_opt_zerocpy;
	bool m_opt_monitor;
	char m_header_code;
	char m_fixed_key;

};