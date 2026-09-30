#pragma once
#include <winsock2.h>
#include <iostream>
#include "NetLibDefine.h"
#include "../Utils/RingBuffer.h"
#include "Packet.h"
#include "../Utils/Profiler.h"
#include "../Utils/LockFreeQueue.h"
#include "../Utils/Logger.h"

#define SEND_PACKET_COUNT 100
class NetLib_Content;

enum SESSION_LEAVE_CODE
{
	NONE,
	WRONG_HEADER = 10,
	WRONG_HEADER_CODE,
	WRONG_HEADER_LEN,
	WRONG_HEADER_CHECKSUM,
	WRONG_RECVPOST_COUNT,
	SEND_FULL = 100,
};

class Session
{
public:
	/// <summary>
	///  SendPost 성공 여부 반환
	/// </summary>
	/// <param name=""></param>
	/// <returns></returns>
	__inline bool TrySendPost()
	{
		//PROFILING("SendPost");
		if (_InterlockedCompareExchange8(&IsSending, 1, 0) == 0)
		{
			PostQueuedCompletionStatus(m_iocpHandle, REQUEST_SENDPOST,
				(ULONG_PTR)this, (WSAOVERLAPPED*)overlapped_sendpost);
			return true;

		}

		return false;
	}
	__inline bool TrySendPostInCompletion()
	{
		//PROFILING("SendPost");
		if (_InterlockedCompareExchange8(&IsSending, 1, 0) == 0)
		{
			if (SendPost() == true)
			{
				// 성공
				return true;
			}
			else
			{
				//InterlockedExchange(&IsSending, 0);

			}

		}

		return false;
	}

	/// <summary>
	/// WSASend 동작 여부 반환
	/// </summary>
	/// <returns></returns>
	__inline bool SendPost()
	{
		//PROFILING("RealSendPost");
		// TODO 사이즈가 0이라면 Post로 보내고 다음 완료통지에서 처리될 수 있게..
		// TODO 세션에 바로 버퍼를 세팅할 수 있게..

		int send_ret;
		DWORD flags = 0;
		Packet* packets[SEND_PACKET_COUNT];
		WSABUF wsabufs[SEND_PACKET_COUNT];
		int totalCnt = 0;
		for (int i = 0; i < SEND_PACKET_COUNT; i++)
		{
			int ret = m_sendBuffer.Dequeue(packets[i]);
			if (ret == -1)
				break;
			totalCnt++;
		}

		if (totalCnt == 0)
		{
			PostQueuedCompletionStatus(m_iocpHandle, REQUEST_SENDING_CHECK, (ULONG_PTR)this, (WSAOVERLAPPED*)overlapped_send);

			return true;
		}

		//if (m_sendPacketCount != 0)
		//{
		//	//ClearSendPackets();
		//	//Packet* curpacket = m_sendPackets[0];
		//	printf("악");
		//}

		int cnt = 0;
		for (;cnt < totalCnt; cnt++)
		{
			Packet* curpacket = packets[cnt];
			wsabufs[cnt].buf = curpacket->GetBufferPtr();
			wsabufs[cnt].len = curpacket->GetDataSize();
			//curpacket->sessionHandle = SessionHandle;
			//curpacket->type = Packet::eType::SENDPOST;
			m_sendPackets[cnt] = curpacket;

		}
		m_sendPacketCount = cnt;
		cumulative_send += m_sendPacketCount;
		send_ret = WSASend(m_socket, wsabufs, cnt, nullptr, flags, (WSAOVERLAPPED*)overlapped_send, nullptr);


		if (send_ret == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
				if (m_IsConnecting == 0)
				{
					printf("Stop");
				}
				// 정상 동작
				if (err == WSAECONNRESET)
				{
					// RST
				}
				else if (err == WSAECONNABORTED)
				{
					// 연결 중단됨
				}
				else if (err == WSAENOTSOCK)
				{
					// 무효화 된 소켓
				}
				else if (err == WSAEINTR)
				{

				}
				// 이상 동작
				else
				{
					printf("WSASend() err: %d\n", err);
					LOG(L"Send", LEVEL_ERROR, L"ERRCODE: %d", err);

				}
				return false;
			}
		}

		return true;
	}
	__inline bool RecvPost()
	{
		DWORD flags = 0;
		WSABUF wsabufs[2];
		DWORD numOfBytes;
		int ret_recv;
		int directSize_recv;
		char* bufptr = m_recvBuffer->GetBufferPtr();
		char* front_recv = m_recvBuffer->GetFrontPtr();
		char* rear_recv = m_recvBuffer->GetRearPtr();

		directSize_recv = m_recvBuffer->DirectEnqueueSize(front_recv, rear_recv);
		flags = 0;

		int freeSize = m_recvBuffer->GetFreeSize(front_recv, rear_recv) - directSize_recv;
		if (directSize_recv == 0 && freeSize == 0)
		{
			return false;
		}

		wsabufs[0].buf = rear_recv;
		wsabufs[0].len = directSize_recv;
		wsabufs[1].buf = bufptr;
		wsabufs[1].len = freeSize;

		ret_recv = WSARecv(m_socket, wsabufs, 2, &numOfBytes, &flags, (WSAOVERLAPPED*)overlapped_recv, NULL);

		if (ret_recv == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
				// 정상 동작
				if (err == WSAECONNRESET)
				{
					// RST
				}
				else if (err == WSAECONNABORTED)
				{
					// 연결 중단됨
				}
				else if (err == WSAENOTSOCK)
				{
					// 무효화 된 소켓
				}
				// 이상 동작
				else
				{
					printf("WSARecv() err: %d\n", err);
					LOG(L"Recv", LEVEL_ERROR, L"ERRCODE: %d", err);
				}
				return false;
			}

		}
		else
		{

		}
		return true;
	}
#pragma warning(push)
#pragma warning(disable : 26495) // C26495 멤버 변수 초기화 경고문 억제
	Session()//, m_sendBuffer(10000)
	{
		m_recvBuffer = new RingBuffer(SESSION_RECV_BUFFER_SIZE);
		m_recvrecvBuffer = new RingBuffer(SESSION_CONTENT_BUFFER_SIZE);
		m_sendPackets = new Packet * [SEND_PACKET_COUNT];
		m_sendPacketCount = 0;
		overlapped_send = new CUSTOM_OVERLAPPED;
		overlapped_recv = new CUSTOM_OVERLAPPED;
		overlapped_sendpost = new CUSTOM_OVERLAPPED;
		overlapped_leave = new CUSTOM_OVERLAPPED;
		overlapped_enter = new CUSTOM_OVERLAPPED;

		overlapped_send->type = OVERLAPPED_TYPE::SESSION;
		overlapped_recv->type = OVERLAPPED_TYPE::SESSION;
		overlapped_sendpost->type = OVERLAPPED_TYPE::SESSION;
		overlapped_leave->type = OVERLAPPED_TYPE::SESSION;
		overlapped_enter->type = OVERLAPPED_TYPE::SESSION;
		completionKey = 0;
		recvPostCnt = 0;
		IsMoving = 0;
		m_content = 0;
	}
#pragma warning(pop)
	void Connect(unsigned long long sessionHandle, SOCKET socket, HANDLE iocpHandle, unsigned long inIP, unsigned short inPort)
	{
		m_iocpHandle = iocpHandle;
		IP = inIP;
		Port = inPort;
		m_leave_code = SESSION_LEAVE_CODE::NONE;
		completionKey = 0;

		InterlockedExchange64((long long*)&SessionHandle, sessionHandle);
		InterlockedExchange64((long long*)&m_socket, socket);
		ZeroMemory(overlapped_recv, sizeof(CUSTOM_OVERLAPPED));
		ZeroMemory(overlapped_send, sizeof(CUSTOM_OVERLAPPED));
		ZeroMemory(overlapped_sendpost, sizeof(CUSTOM_OVERLAPPED));
		ZeroMemory(overlapped_leave, sizeof(CUSTOM_OVERLAPPED));
		ZeroMemory(overlapped_enter, sizeof(CUSTOM_OVERLAPPED));

		InterlockedExchange8((char*)&IsMoving, 0);
		InterlockedExchange8(&IsSending, 0);
		InterlockedExchange8((char*)&m_IsConnecting, 1);
		InterlockedExchange16((short*)&IOCount, 1);
		InterlockedExchange8((char*)&requestDisconnect, 0);
		cumulative_recv = 0;
		cumulative_send = 0;
		tps_send = 0;
		tps_recv = 0;
		recvPostCnt = 0;
		// 고민: clear하기 전에 남은게 있다면 누수아닌가?
		m_recvBuffer->ClearBuffer();
		ClearSendPackets();
		ClearSendBuffer();
		ClearContentQueue();
		InterlockedExchange64((long long*)&m_content, 0);
		//m_sendBuffer.ClearBuffer(); 아래 코드로 대체


	}
	void ClearSendBuffer()
	{
		Packet* packet;
		while (1)
		{
			int ret = m_sendBuffer.Dequeue(packet);
			if (ret == -1)
				break;
			long interRet = InterlockedDecrement(&packet->refCount);
			// 아직 SendPost되지 않았다면 refCount는 올라가지 않은 상태일 수 있다.
			if (interRet == 0)
				Packet::Free((Packet*)packet);
			else if (interRet < 0)
				printf("중복시도감지");
		}
		return;
	}
	void ClearSendPackets()
	{
		int g = m_sendPacketCount;
		m_sendPacketCount = 0;
		for (int i = 0; i < g; i++)
		{
			Packet* packet = m_sendPackets[i];
			long interRet = InterlockedDecrement(&packet->refCount);
			if (interRet == 0)
				Packet::Free((Packet*)packet);
			else if (interRet < 0)
				printf("중복시도감지");
		}

	}
	void ClearContentQueue()
	{
		//Packet* packet;
		//while (1)
		//{
		//	if (m_contentQueue.Dequeue(packet) == -1)
		//		break;
		//	Packet::Free(packet);
		//}

		m_recvrecvBuffer->ClearBuffer();
	}

	__inline static void DecodeIOCount(unsigned short inputIOCount, unsigned char& outReleaseFlag, unsigned char& outIOCount)
	{
		outReleaseFlag = inputIOCount >> 8;
		outIOCount = inputIOCount & 0b0000000011111111;
	}

	// 거의 읽기 전용
	unsigned long long SessionHandle; // [2: Index 6: ID]][]
	SOCKET m_socket;
	NetLib_Content* m_content;
	HANDLE m_iocpHandle;
	unsigned long IP;
	unsigned short Port;
	bool IsMoving;
	char IsSending;
	RingBuffer* m_recvrecvBuffer;
	CUSTOM_OVERLAPPED* overlapped_send;
	CUSTOM_OVERLAPPED* overlapped_recv;
	CUSTOM_OVERLAPPED* overlapped_sendpost;
	CUSTOM_OVERLAPPED* overlapped_leave;
	CUSTOM_OVERLAPPED* overlapped_enter;
	RingBuffer* m_recvBuffer;
	long tps_send;
	long tps_recv;
	long m_sendPacketCount;
	unsigned short IOCount;
	unsigned char m_IsConnecting;
	unsigned char requestDisconnect;
	LockFreeQueue<Packet*> m_sendBuffer;
	Packet** m_sendPackets;
	static const unsigned short RELEASE_IOCOUNT = 0xEE00;
	static const unsigned char	RELEASE_FLAG = 0xEE;
	static const DWORD  REQUEST_SENDPOST = 0x53547426;
	static const DWORD  REQUEST_SENDING_CHECK = 0x990130;
	static const DWORD  REQUEST_RELEASE = 0x72002989;
	static const DWORD  REQUEST_ENTER = 0x55555555;


	//LockFreeQueue<Packet*> m_contentQueue;

	//Packet* m_sendPackets[SEND_PACKET_COUNT];
	long cumulative_recv;
	long cumulative_send;

	long recvPostCnt;
	SESSION_LEAVE_CODE m_leave_code;
	void* completionKey;

};