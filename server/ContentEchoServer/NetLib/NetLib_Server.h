#pragma once
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Winmm.lib")

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
#include "NetLibraryProtocol.h"
#include "../Utils/LockFreeQueue.h"

#include "NetLib_Content.h"
#include "NetLib_Helper.h"
class NetLib_Server
{
public:
	NetLib_Server(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets);
	virtual ~NetLib_Server();

	void Start();
	void AcceptPause();
	void Stop();

	__inline int GetPacketUseSize() { return Packet::GetPoolUseSize(); }
	__inline int GetSessionCount() { return m_sessionCount; }
	__inline int GetDisconncectCount() { return m_disconnectCount; }

	bool SendPacket(unsigned long long sessionHandle, Packet* packet);
	bool SendPacketMulticast(unsigned long long* sessionsHandles, long sessionCount, Packet* packet);
	bool SendPacketFast(unsigned long long sessionHandle, Packet* packet);
	bool Disconnect(unsigned long long sessionID);

	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port) { return false; }
	virtual void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port) {}
	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) {}
	virtual void OnRecv(unsigned long long sessionHandle, Packet* packet) {}
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
	int GetTotal_Accept() { return m_total_accept; }
	int GetTPS_Accept() { return m_tps_accept; }
	int GetTPS_Recv() { return m_tps_recv_last; }
	int GetTPS_Send() { return m_tps_send_last; }
	// 최근 1초 동안 송수신한 바이트 (세션 소켓 기준, 헤더 포함)
	long long GetBPS_Recv() { return m_bytes_recv_last; }
	long long GetBPS_Send() { return m_bytes_send_last; }

	int GetSessionTPS_Send(unsigned long long sessionHandle);
	int GetSessionTPS_Recv(unsigned long long sessionHandle);

	void DisconnectAll();
	void RegistContent(NetLib_Content* content);
	void UnRegistContent(NetLib_Content* content); // 제거는 블로킹
	bool Move_Content(NetLib_Content* content, unsigned long long sessionHandle, void* completionKey = nullptr);
	__forceinline Session* FindSessionWithoutIOCount(unsigned long long sessionHandle)
	{
		unsigned short index;
		DecodeSessionHandle(sessionHandle, &index, nullptr);

		if (index >= m_opt_maxOfSession) [[unlikely]]
			return nullptr;

		Session* session = &m_sessions[index];

		if (session->SessionHandle != sessionHandle)
		{
			return nullptr;

		}


		return session;
	}
	__forceinline bool SendPacketFastWithoutIOCount(unsigned long long sessionHandle, Packet* sendpacket)
	{
		Session* session = FindSessionWithoutIOCount(sessionHandle);

		if (session == nullptr) [[unlikely]]
		{
			Packet::Free(sendpacket);
			return false;
		}

		long sendqueue_size = session->m_sendBuffer.GetUseSize();
		//if (max_send < sendqueue_size)
		//{
		//	InterlockedExchange(&max_send, sendqueue_size);
		//	LOG(L"SendQueue_Max", LEVEL_ERROR, L"maxsize: %d", sendqueue_size);
		//}
		if (sendqueue_size > m_opt_maxOfSendPackets) [[unlikely]]
		{
			session->m_leave_code = SESSION_LEAVE_CODE::SEND_FULL;
			Disconnect(sessionHandle);
			Packet::Free(sendpacket);

			return false;
		}

		//printf("세션: 0x%16llx 수신데이터: %llu\n",sessionHandle, (unsigned long long)*buf);
		//sendpacket->type = Packet::eType::SENDPACKETFAST;
		InterlockedIncrement(&sendpacket->refCount);
		NetHeader* header = sendpacket->headerPtr;
		header->Code = m_header_code;
		header->Len = sendpacket->GetPayloadSize();

		WORD type = *(WORD*)(sendpacket->GetPayloadPtr());
		if (m_opt_encryption == 1) [[likely]]
		{
			//PROFILING("Encode");
			// 페이로드 인코딩
			header->RandKey = NetLib_Helper::FastRand();
			SimpleEncoder encoder;
			encoder.SetBuffers(&sendpacket->headerPtr->CheckSum, &sendpacket->headerPtr->CheckSum, header->Len + 1);
			header->CheckSum = encoder.CalculateChecksum(sendpacket->GetPayloadPtr(), header->Len);
			encoder.SetKeys(m_fixed_key, header->RandKey);
			encoder.Encode();
		}

		session->m_sendBuffer.Enqueue(sendpacket);


		if (Increment_IOCount(session) == false)
		{
			if (session->TrySendPost() == false)
			{
				if (Decrement_IOCount(session) == true)
					return false;

			}
		}

		return true;
	}
protected:

private:
	friend class NetLib_Content;
	__inline void CreateSessionHandle(unsigned short index, unsigned long long sessionID, unsigned long long* outSessionHandle);
	__inline void DecodeSessionHandle(unsigned long long sessionHandle, unsigned short* outIndex, unsigned long long* outSessionID);

	/// <summary>
	///  Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	__inline bool Increment_IOCount(Session* session);

	/// <summary>
	/// Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	__inline bool Decrement_IOCount(Session* session);
	void ReleaseSession(Session* session);


	static unsigned int __stdcall WorkerThread(void* argv);
	static unsigned int __stdcall AcceptThread(void* argv);
	static unsigned int __stdcall MonitorThread(void* argv);
	static unsigned int __stdcall SchedulerThread(void* argv);
	float WaitForTime(int tick, DWORD* prevTime);
	Session* m_sessions;
	HANDLE m_iocpHandle;
	HANDLE m_acceptThread;
	HANDLE m_monitorThread;
	HANDLE m_frequencyThread;
	HANDLE m_threadPool[16];
	SOCKET m_listen_socket;
	SOCKADDR_IN m_listen_addr;
	LockFreeStack<unsigned short> m_sessionIndexPool;
	LockFreeQueue <TPS_SET> m_tpsSets;
	unsigned long long m_idPool;
	unsigned long m_sessionCount;
	unsigned long m_disconnectCount;
	int m_errCode;
	bool m_isListening;
	bool m_isRunning;

	long m_total_accept;

	long m_cumulate_accept;
	long m_tps_accept;
	long m_tps_recv_cumulate;
	long m_tps_send_cumulate;
	long m_tps_recv_last;
	long m_tps_send_last;
	long long m_bytes_recv_cumulate;
	long long m_bytes_send_cumulate;
	long long m_bytes_recv_last;
	long long m_bytes_send_last;
	int m_opt_workerTH_count;
	int m_opt_concurrentTH_size;
	int m_opt_maxOfSession;
	bool m_opt_encryption;
	bool m_opt_zerocpy;
	int m_opt_maxOfSendPackets;
	char m_header_code;
	char m_fixed_key;
	long max_send;
	long max_recvPostCnt;
	std::unordered_map<unsigned long long, NetLib_Content*> m_registered_contents; // <id, ptr>
	SRWLOCK m_lock_registered_contents;


};