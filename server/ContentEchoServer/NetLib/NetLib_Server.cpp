#include <iostream>
#include "NetLib_Server.h"
#include "../Utils/Logger.h"
unsigned int __stdcall NetLib_Server::WorkerThread(void* argv)
{
	NetLib_Server* server = (NetLib_Server*)argv;
	//printf("워커스레드 시작 랜서버: %p\n", server);
	SimpleEncoder decoder;
	// 페이로드 디코딩

	while (1)
	{
		DWORD transferred = 0;
		void* completionKey = 0;
		CUSTOM_OVERLAPPED* overlapped = 0;
		int gqcs_ret;

		gqcs_ret = GetQueuedCompletionStatus(server->m_iocpHandle, &transferred, (PULONG_PTR)&completionKey,
			(LPOVERLAPPED*)&overlapped, INFINITE);

		// 종료 로직
		//if (transferred == 0 && (__int64)completionKey == 0 && (__int64)overlapped == 0)
		//	break;

		if (overlapped->type == OVERLAPPED_TYPE::SESSION)
		{
			Session* session = (Session*)completionKey;
			if (overlapped == session->overlapped_send)
			{
				if (transferred != 0)
				{
					if (transferred != Session::REQUEST_SENDING_CHECK)
					{
						session->ClearSendPackets();
						InterlockedAdd64(&server->m_bytes_send_cumulate, transferred);
					}

					_InterlockedCompareExchange8(&session->IsSending, 0, 1);

					if (session->m_sendBuffer.GetUseSize() > 0)
					{
						if (server->Increment_IOCount(session) == false)
						{
							if (session->TrySendPostInCompletion() == false)
							{
								server->Decrement_IOCount(session);

							}

						}

					}
					server->Decrement_IOCount(session);
				}
				else
				{
					server->Decrement_IOCount(session);
					//continue;
				}
			}// end if(send)
			else if (overlapped == session->overlapped_recv)
			{
				if (transferred == 0)
				{
					server->Decrement_IOCount(session);
					continue;
				}

				RingBuffer* currRecvBuffer = session->m_recvBuffer;
				RingBuffer* currContentBuffer = session->m_recvrecvBuffer;
				// 수신 완료 작업
				currRecvBuffer->MoveRear(transferred);
				InterlockedAdd64(&server->m_bytes_recv_cumulate, transferred);

				while (1)
				{
					if (++session->recvPostCnt > server->max_recvPostCnt) [[unlikely]]
					{
						WCHAR IPPort[32];
						NetLib_Helper::IPPortToWstring(session->IP, session->Port, IPPort, 32);
						LOG(L"Wrong_RecvPost_Count", LEVEL_DEBUG, L"%s Wrong Recv Post Count", IPPort);
						session->m_leave_code = WRONG_RECVPOST_COUNT;
						server->Disconnect(session->SessionHandle);
						break;
					}

					char iobuf[sizeof(NetHeader) + PAYLOAD_LEN_DEFAULT];
					if (currRecvBuffer->GetUseSize() < sizeof(NetHeader)) break; // 헤더 까보기
					NetHeader* header = (NetHeader*)iobuf;
					char* payload = iobuf + sizeof(NetHeader);
					int ret_peek = currRecvBuffer->Peek((char*)header, sizeof(NetHeader));

					if (header->Code != server->m_header_code)[[unlikely]]

					{
						WCHAR IPPort[32];
						NetLib_Helper::IPPortToWstring(session->IP, session->Port, IPPort, 32);
						LOG(L"Wrong_Header_Code", LEVEL_DEBUG, L"%s Wrong Header Code = %d", IPPort, header->Code);
						session->m_leave_code = WRONG_HEADER_CODE;
						server->Disconnect(session->SessionHandle);
						break;
					}

					if (header->Len > PAYLOAD_LEN_DEFAULT)[[unlikely]]
					{
						WCHAR IPPort[32];
						NetLib_Helper::IPPortToWstring(session->IP, session->Port, IPPort, 32);
						LOG(L"Wrong_Header_Len", LEVEL_DEBUG, L"%s Wrong Header Len = %d", IPPort, header->Len);
						session->m_leave_code = WRONG_HEADER_LEN;
						server->Disconnect(session->SessionHandle);
						break;
					}

					// 데이터 있으면 패킷으로 카피
					if (currRecvBuffer->GetUseSize() < sizeof(NetHeader) + header->Len) break;
					currRecvBuffer->MoveFront(sizeof(NetHeader));

					// 바이트스트림 링버퍼라 카피떠야 한번에 빼기가 편함
					int ret_deq = currRecvBuffer->Dequeue(payload, header->Len);

					if (server->m_opt_encryption)
					{

						decoder.SetBuffers(&header->CheckSum, &header->CheckSum, header->Len + 1);
						decoder.SetKeys(server->m_fixed_key, header->RandKey);
						decoder.Decode();
						unsigned char checkSum = decoder.CalculateChecksum((unsigned char*)payload, header->Len);

						if (checkSum != header->CheckSum) [[unlikely]]
						{
							WCHAR IPPort[32];
							NetLib_Helper::IPPortToWstring(session->IP, session->Port, IPPort, 32);
							LOG(L"Wrong_Header_CheckSum", LEVEL_DEBUG, L"%s Wrong Checksum", IPPort);
							session->m_leave_code = WRONG_HEADER_CHECKSUM;
							server->Disconnect(session->SessionHandle);
							break;
						}

					}

					session->cumulative_recv++;
					session->recvPostCnt = 0;
					if (session->m_content == nullptr)
					{
						Packet* packet = Packet::Alloc();
						//packet->type = Packet::eType::RECVPROC;
						packet->PutData(payload, header->Len);
						server->OnRecv(session->SessionHandle, packet);
						Packet::Free(packet);
					}
					else
					{
						ContentQueueHeader cheader{header->Len};
						if (currContentBuffer->Enqueue((char*)&cheader, sizeof(ContentQueueHeader)) == 0 ||
							currContentBuffer->Enqueue(payload, header->Len) == 0) [[unlikely]]
						{
							server->Disconnect(session->SessionHandle);
							break;
						}

						//session->m_contentQueue.Enqueue(packet);
					}
				}
				if (server->Increment_IOCount(session) == true)
				{
					continue;
				}
				// 다음을 위한 리시브
				if (session->RecvPost() == false)
				{
					server->Decrement_IOCount(session);
				}
				else
				{
					if (session->requestDisconnect == true)
					{
						CancelIoEx((HANDLE)session->m_socket, nullptr);
					}
				}
				server->Decrement_IOCount(session);

			} // end if(recv)
			else if (overlapped == session->overlapped_sendpost)
			{
				if (session->SendPost() == true)
				{
				}
				else
				{
					server->Decrement_IOCount(session);
				}
				continue;
			}

			else if (overlapped == session->overlapped_leave)
			{
				server->OnClientLeave(session->SessionHandle, session->m_leave_code, session->IP, session->Port);

				unsigned short index;
				server->DecodeSessionHandle(session->SessionHandle, &index, nullptr);
				server->m_sessionIndexPool.Push(index);
				InterlockedDecrement(&server->m_sessionCount);
				continue;
			}
			else if (overlapped == session->overlapped_enter)
			{
				CreateIoCompletionPort((HANDLE)session->m_socket, server->m_iocpHandle, (unsigned long long)session, 0);

				server->OnClientJoin(session->SessionHandle, session->IP, session->Port);
				// Recv 시작
				if (session->RecvPost() == true)
				{

				}
				else
				{
					if (server->Decrement_IOCount(session) == true)
					{
						//printf("aa\n");
						//printf("recv 릴리즈에 늦게들어온 놈이 CAS 자체를 재사용 뒤에 하면서 새로 연결된 세션이 끊어질 가능성이 있다. %d\n", ++server->m_disconnectCount);

					}

				}
				continue;
			}
		}
		else if (overlapped->type == OVERLAPPED_TYPE::CONTENT)
		{
			NetLib_Content* content = (NetLib_Content*)completionKey;
			if (transferred == NetLib_Content::REQUEST_CONTENT_UPDATE)
			{
				DWORD now = timeGetTime();
				DWORD cd = now - content->m_lastUpdate;
				while (cd >= content->m_msFrequency) 
				{
					content->m_lastUpdate += content->m_msFrequency;
					content->m_lastDeltaTime = (float)content->m_msFrequency / 1000.0f;
					content->Update(content->m_lastDeltaTime);
					cd -= content->m_msFrequency;
				}

				PostQueuedCompletionStatus(server->m_iocpHandle, NetLib_Content::REQUEST_CONTENT_UPDATE,
					(ULONG_PTR)content, (WSAOVERLAPPED*)content->overlapped_content_update);
			}
			else if (transferred == NetLib_Content::REQUEST_CONTENT_BEGIN)
			{
				content->OnBegin();
				content->m_lastUpdate = timeGetTime();
				content->m_server = server;
				//content->IsUpdating = false;
				//server->m_registered_contents[content->GetID()] = content;
				PostQueuedCompletionStatus(server->m_iocpHandle, NetLib_Content::REQUEST_CONTENT_UPDATE,
					(ULONG_PTR)content, (WSAOVERLAPPED*)content->overlapped_content_update);
				//AcquireSRWLockExclusive(&server->m_lock_registered_contents);
				//// 없어야 등록
				////auto itRegistered = server->m_registered_contents.find(content->GetID());
				////if (itRegistered == server->m_registered_contents.end())
				////{
				//content->m_server = server;
				//content->IsUpdating = false;
				//server->m_registered_contents[content->GetID()] = content;
				////}
				//ReleaseSRWLockExclusive(&server->m_lock_registered_contents);
			}
		}
	}

	printf("워커 스레드 종료\n");
	return 0;
}

unsigned int __stdcall NetLib_Server::AcceptThread(void* argv)
{
	NetLib_Server* server = (NetLib_Server*)argv;
	//printf("Accept스레드 시작 랜서버: %p\n", server);

	server->m_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server->m_listen_socket == INVALID_SOCKET)
	{
		printf("socket() error\n");
	}

	LINGER optval;
	optval.l_onoff = 1;
	optval.l_linger = 0;
	int ret_setsockopt = setsockopt(server->m_listen_socket, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
	if (ret_setsockopt == INVALID_SOCKET)
	{
		int err_setsockopt = WSAGetLastError();
	}

	int ret_bind = bind(server->m_listen_socket, (SOCKADDR*)&server->m_listen_addr, sizeof(SOCKADDR));

	if (ret_bind == SOCKET_ERROR)
	{
		printf("bind err\n");
	}

	int ret_listen = listen(server->m_listen_socket, SOMAXCONN_HINT(32768));
	if (ret_listen == SOCKET_ERROR)
	{
		printf("listen err\n");
		LOG(L"listen", LEVEL_DEBUG, L"err: %d", WSAGetLastError());

	}

	SOCKET newsock;
	SOCKADDR_IN clientaddr;
	int addrlen;

	while (1)
	{
		addrlen = sizeof(clientaddr);
		newsock = accept(server->m_listen_socket, (SOCKADDR*)&clientaddr, &addrlen);
		if (newsock == INVALID_SOCKET)
		{
			LOG(L"Accept_ERR", LEVEL_ERROR, L"err: %d", WSAGetLastError());

			continue;
		}
		InterlockedIncrement(&server->m_cumulate_accept);
		if (server->m_sessionCount >= server->m_opt_maxOfSession)
		{
			closesocket(newsock);
			LOG(L"Accept", LEVEL_DEBUG, L"Sessions has full.");
			continue;
		}
		unsigned long IP = ntohl(clientaddr.sin_addr.S_un.S_addr);
		unsigned short port = ntohs(clientaddr.sin_port);

		{
			if (server->OnConnectionRequest(IP, port) == false)
			{
				closesocket(newsock);
				continue;
			}
		}

		// 제로카피
		if (server->m_opt_zerocpy)
		{
			int size = 0;
			setsockopt(newsock, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size));

		}
		Session* newSession;
		// 스택에서 인덱스 팝
		{
			unsigned short index;
			server->m_sessionIndexPool.Pop(&index);

			InterlockedIncrement(&server->m_sessionCount);
			// 해당 인덱스에 새로운 세션 할당
			newSession = &server->m_sessions[index];

			long long allocID = InterlockedIncrement64((long long*)&server->m_idPool); // 세션아이디
			unsigned long long newSessionHandle;
			server->CreateSessionHandle(index, allocID, &newSessionHandle); // 인덱스와 세션아이디로 핸들 생성
			newSession->Connect(newSessionHandle, newsock, server->m_iocpHandle, IP, port);
		}
		{
			PostQueuedCompletionStatus(server->m_iocpHandle, Session::REQUEST_ENTER, (ULONG_PTR)newSession, (WSAOVERLAPPED*)newSession->overlapped_enter);
		}

	}
	printf("Accept 스레드 종료\n");
	return 0;
}
unsigned int __stdcall NetLib_Server::MonitorThread(void* argv)
{
	NetLib_Server* server = (NetLib_Server*)argv;
	DWORD prevTime = timeGetTime();
	while (server->m_isRunning)
	{
		long sum_recv = 0;
		long sum_send = 0;
		server->m_tps_accept = InterlockedExchange(&server->m_cumulate_accept, 0);
		server->m_total_accept += server->m_tps_accept;

		for (int i = 0; i < server->m_opt_maxOfSession; i++)
		{
			Session* session = &server->m_sessions[i];
			if (session->m_IsConnecting == 1)
			{
				session->tps_recv = InterlockedExchange(&session->cumulative_recv, 0);
				session->tps_send = InterlockedExchange(&session->cumulative_send, 0);

				sum_recv += session->tps_recv;
				sum_send += session->tps_send;

			}
		}

		// Release된 세션의 TPS도 더해주기
		int size = server->m_tpsSets.GetUseSize();
		for (int i = 0; i < size; i++)
		{
			TPS_SET ret;
			server->m_tpsSets.Dequeue(ret);
			sum_send += ret.send;
			sum_recv += ret.recv;
		}
		server->m_tps_recv_cumulate += sum_recv;
		server->m_tps_send_cumulate += sum_send;

		server->m_tps_recv_last = server->m_tps_recv_cumulate;
		server->m_tps_send_last = server->m_tps_send_cumulate;
		server->m_tps_recv_cumulate = 0;
		server->m_tps_send_cumulate = 0;
		server->m_bytes_recv_last = InterlockedExchange64(&server->m_bytes_recv_cumulate, 0);
		server->m_bytes_send_last = InterlockedExchange64(&server->m_bytes_send_cumulate, 0);

		server->WaitForTime(1000, &prevTime);

	}
	printf("모니터스레드 종료\n");
	return 0;
}
/// <summary>
/// 리턴이 false라면 센드에 실패했음을 알림
/// </summary>
/// <param name="ID"></param>
/// <param name="packet"></param>
/// <returns></returns>
bool NetLib_Server::SendPacket(unsigned long long sessionHandle, Packet* packet)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return false;
	long sendqueue_size = session->m_sendBuffer.GetUseSize();
	if (max_send < sendqueue_size)
	{
		InterlockedExchange(&max_send, sendqueue_size);
		LOG(L"SendQueue_Max", LEVEL_ERROR, L"maxsize: %d", sendqueue_size);
	}
	if (sendqueue_size > m_opt_maxOfSendPackets)
	{
		session->m_leave_code = SESSION_LEAVE_CODE::SEND_FULL;
		ReturnSession(session);
		Disconnect(sessionHandle);
		return false;
	}

	//printf("세션: 0x%16llx 수신데이터: %llu\n",sessionHandle, (unsigned long long)*buf);
	Packet* sendpacket = Packet::NetAlloc();
	int len = packet->GetDataSize();
	//sendpacket->type = Packet::eType::SENDPACKET;
	InterlockedIncrement(&sendpacket->refCount);
	NetHeader* header = sendpacket->headerPtr;
	header->Code = m_header_code;

	sendpacket->PutData(packet->GetBufferPtr(), len);

	header->Len = sendpacket->GetPayloadSize();

	WORD type = *(WORD*)(sendpacket->GetPayloadPtr());
	if (m_opt_encryption == 1)
	{
		// 페이로드 인코딩
		header->RandKey = rand();
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

	if (ReturnSession(session) == true)
		return false;

	return true;
}

bool NetLib_Server::SendPacketMulticast(unsigned long long* sessionHandles, long sessionCount, Packet* packet)
{
	if (sessionCount == 0) return false;

	bool ret = true;
	int len = packet->GetDataSize();
	Packet* sendpacket = Packet::NetAlloc();
	//sendpacket->type = Packet::eType::SENDPACK_MULTICAST;

	NetHeader* header = sendpacket->headerPtr;
	header->Code = m_header_code;

	sendpacket->PutData(packet->GetBufferPtr(), len);
	header->Len = sendpacket->GetPayloadSize();
	//InterlockedIncrement(&sendpacket->refCount);
	InterlockedAdd(&sendpacket->refCount, sessionCount);
	WORD type = *(WORD*)(sendpacket->GetPayloadPtr());

	if (m_opt_encryption == 1)
	{
		// 페이로드 인코딩
		header->RandKey = rand();
		SimpleEncoder encoder;
		encoder.SetBuffers(&sendpacket->headerPtr->CheckSum, &sendpacket->headerPtr->CheckSum, header->Len + 1);
		header->CheckSum = encoder.CalculateChecksum(sendpacket->GetPayloadPtr(), header->Len);
		encoder.SetKeys(m_fixed_key, header->RandKey);
		encoder.Encode();
	}

	for (int i = 0; i < sessionCount; i++)
	{
		Session* session = FindSession(sessionHandles[i]);
		if (session == nullptr)
		{
			//Packet::NetFree(packet);
			long interRet = InterlockedDecrement(&sendpacket->refCount);
			if (interRet == 0)
				Packet::Free(sendpacket);
			else if (interRet < 0)
				printf("중복시도감지");
			ret = false;
			continue;
		}
		//InterlockedIncrement(&sendpacket->refCount);

		if (session->m_sendBuffer.GetUseSize() > m_opt_maxOfSendPackets)
		{
			session->m_leave_code = SESSION_LEAVE_CODE::SEND_FULL;
			ReturnSession(session);
			Disconnect(sessionHandles[i]);

			long interRet = InterlockedDecrement(&sendpacket->refCount);
			if (interRet == 0)
				Packet::Free(sendpacket);
			else if (interRet < 0)
				printf("중복시도감지");
			ret = false;

			continue;
		}

		session->m_sendBuffer.Enqueue(sendpacket);
		if (Increment_IOCount(session) == false)
		{
			if (session->TrySendPost() == false)
			{
				if (Decrement_IOCount(session) == true)
				{
					continue;
				}
			}
		}

		ReturnSession(session);
		//long interRet = InterlockedDecrement(&sendpacket->refCount);
		//if (interRet == 0)
		//	Packet::Free(sendpacket);
		//else if (interRet < 0)
		//	printf("중복시도감지");
	}

	return ret;
}

bool NetLib_Server::SendPacketFast(unsigned long long sessionHandle, Packet* sendpacket)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
	{
		Packet::Free(sendpacket);
		return false;
	}
	long sendqueue_size = session->m_sendBuffer.GetUseSize();
	if (max_send < sendqueue_size)
	{
		InterlockedExchange(&max_send, sendqueue_size);
		LOG(L"SendQueue_Max", LEVEL_ERROR, L"maxsize: %d", sendqueue_size);
	}
	if (sendqueue_size > m_opt_maxOfSendPackets)
	{
		session->m_leave_code = SESSION_LEAVE_CODE::SEND_FULL;
		ReturnSession(session);
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
	if (m_opt_encryption == 1)
	{
		// 페이로드 인코딩
		header->RandKey = rand();
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

	if (ReturnSession(session) == true)
		return false;

	return true;
}



bool NetLib_Server::Disconnect(unsigned long long sessionHandle)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return false;
	//printf("끊기요청0x%llx\n", sessionHandle);
	InterlockedExchange8((char*)&session->requestDisconnect, 1);
	CancelIoEx((HANDLE)session->m_socket, nullptr);
	ReturnSession(session);
	return true;
}

NetLib_Server::NetLib_Server(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets)
{
	timeBeginPeriod(1);
	max_send = INT_MIN;

	m_sessionCount = 0;
	m_errCode = 0;
	m_idPool = 0;
	m_isListening = 0;
	m_acceptThread = 0;
	m_monitorThread = 0;
	m_listen_socket = 0;
	m_isRunning = 0;
	m_tps_accept = 0;
	m_tps_recv_cumulate = 0;
	m_tps_send_cumulate = 0;
	m_tps_recv_last = 0;
	m_tps_send_last = 0;
	m_bytes_recv_cumulate = 0;
	m_bytes_send_cumulate = 0;
	m_bytes_recv_last = 0;
	m_bytes_send_last = 0;
	m_total_accept = 0;
	m_frequencyThread = 0;
	m_opt_maxOfSession = opt_maxOfSession;
	m_opt_maxOfSendPackets = opt_maxOfSendPackets;
	m_sessions = new Session[m_opt_maxOfSession];
	max_recvPostCnt = 10;
	m_listen_addr;
	m_opt_workerTH_count = opt_workerTH_Pool_size;
	m_opt_concurrentTH_size = opt_concurrentTH_size;
	ZeroMemory(&m_listen_addr, sizeof(SOCKADDR_IN));
	unsigned long ip;
	InetPtonW(AF_INET, openIP, &ip);
	m_listen_addr.sin_family = AF_INET;
	m_listen_addr.sin_addr.s_addr = htonl(ip);
	m_listen_addr.sin_port = htons(openPort);
	m_disconnectCount = 0;
	m_opt_zerocpy = opt_zerocpy;
	// opt_encryption == nullptr: 인코딩/체크섬 미사용. 헤더 Code는 기본값(0x77)으로 계속 검사한다.
	m_opt_encryption = false;
	{
		Opt_Encryption defaultOpt;
		m_header_code = defaultOpt.Header_Code;
		m_fixed_key = defaultOpt.Fixed_Key;
	}
	if (opt_encryption != nullptr)
	{
		m_header_code = opt_encryption->Header_Code;
		m_fixed_key = opt_encryption->Fixed_Key;
		m_opt_encryption = true;
	}

	for (short i = 0; i < opt_maxOfSession; i++)
	{
		int r = opt_maxOfSession - 1 - i;
		m_sessionIndexPool.Push(r);
	}

	WSADATA wsa;
	m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, opt_concurrentTH_size);

	for (int i = 0; i < opt_workerTH_Pool_size; i++)
	{
		m_threadPool[i] = (HANDLE)_beginthreadex(0, 0, WorkerThread, this, 0, 0);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("\n\n\n\n\nWSAStartUp Failed... code: %d\n\n\n\n\n", GetLastError());

	}
}

NetLib_Server::~NetLib_Server()
{

}

void NetLib_Server::Start()
{
	LOG(L"ServerState", LEVEL_DEBUG, L"Server Start");

	m_isRunning = 1;
	m_acceptThread = (HANDLE)_beginthreadex(0, 0, AcceptThread, this, 0, 0);

	m_isListening = true;
	m_monitorThread = (HANDLE)_beginthreadex(0, 0, MonitorThread, this, 0, 0);
}

void NetLib_Server::AcceptPause()
{
	LOG(L"ServerState", LEVEL_DEBUG, L"Server AcceptPause");

	closesocket(m_listen_socket);
	WaitForSingleObject(m_acceptThread, INFINITE);
	m_isListening = false;
}

void NetLib_Server::Stop()
{
	m_isRunning = 0;
	if (m_isListening)
		AcceptPause();

	for (int i = 0; i < m_opt_workerTH_count; i++)
	{
		PostQueuedCompletionStatus(m_iocpHandle, 0, 0, 0);
	}
	WaitForMultipleObjects(m_opt_workerTH_count, m_threadPool, true, INFINITE);

	for (int i = 0; i < m_opt_workerTH_count; i++)
	{
		CloseHandle(m_threadPool[i]);
		printf("[%d]번 스레드 반환\n", i);
	}
	CloseHandle(m_iocpHandle);
	printf("IOCP핸들 반환\n");

	WSACleanup();
}

__inline Session* NetLib_Server::FindSession(unsigned long long sessionHandle)
{
	unsigned short index;
	DecodeSessionHandle(sessionHandle, &index, nullptr);

	if (index >= m_opt_maxOfSession)
		return nullptr;

	Session* session = &m_sessions[index];

	if (Increment_IOCount(session) == true)
	{
		// 플래그에 손대지 말고 나가자
		// Decrement_IOCount(session);
		return nullptr;
	}

	if (session->SessionHandle != sessionHandle)
	{
		Decrement_IOCount(session);
		return nullptr;

	}


	return session;
}


/// <summary>
/// 세션이 릴리즈 예정이라면 true 반환.
/// 만약 true를 반환했다면 이후 플래그를 변경하지 말아야 한다.
/// </summary>
/// <param name="session"></param>
/// <returns></returns>
bool NetLib_Server::Increment_IOCount(Session* session)
{
	unsigned short currIO = InterlockedIncrement16((short*)&session->IOCount);
	unsigned char flag;
	unsigned char ioCount;
	Session::DecodeIOCount(currIO, flag, ioCount);

	return flag;
	//return false;
}

bool NetLib_Server::Decrement_IOCount(Session* session)
{
	bool ret = false;
	unsigned short currIO = InterlockedDecrement16((short*)&session->IOCount);
	//unsigned char flag = -1;
	//unsigned char ioCount = -1;
	//Session::DecodeIOCount(currIO, flag, ioCount);

	if (currIO == 0)
	{
		ReleaseSession(session);
		ret = true;
	}


	return ret;
}

void NetLib_Server::ReleaseSession(Session* session)
{
	if (InterlockedCompareExchange16((short*)&session->IOCount, Session::RELEASE_IOCOUNT, 0) != 0)
	{
		//printf("중복해제감지");
		return;
	}
	if (session->m_content == nullptr)
	{
		InterlockedExchange8((char*)&session->m_IsConnecting, 0);
		closesocket(session->m_socket);
		session->m_socket = INVALID_SOCKET;
		session->m_recvBuffer->ClearBuffer();
		session->ClearSendPackets();
		session->ClearSendBuffer();
		session->ClearContentQueue();
		InterlockedExchange8(&session->IsSending, 0);
		InterlockedExchange8((char*)&session->IsMoving, 0);
		InterlockedExchange64((long long*)&session->m_content, 0);
		m_tpsSets.Enqueue({ session->cumulative_send, session->cumulative_recv });

		PostQueuedCompletionStatus(m_iocpHandle, 0, (ULONG_PTR)session, (LPOVERLAPPED)session->overlapped_leave);

	}
	else
	{
		ContentMSG msg;
		msg.type = 2;
		msg.session = (void*)session;
		session->m_content->m_msgQueue.Enqueue(msg);
	}

}

int NetLib_Server::GetSessionTPS_Send(unsigned long long sessionHandle)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return 0;

	int ret = session->tps_send;

	ReturnSession(session);
	return ret;
}
int NetLib_Server::GetSessionTPS_Recv(unsigned long long sessionHandle)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return 0;

	int ret = session->tps_recv;
	ReturnSession(session);
	return ret;
}

__inline void NetLib_Server::CreateSessionHandle(unsigned short index, unsigned long long sessionID, unsigned long long* outSessionHandle)
{
	unsigned long long IDUpper = index;
	IDUpper = IDUpper << 48;

	unsigned long long IDLower = sessionID;
	IDLower = IDLower & 0x0000ffffffffffff;

	*outSessionHandle = IDUpper | IDLower;
}

__inline void NetLib_Server::DecodeSessionHandle(unsigned long long sessionHandle, unsigned short* outIndex, unsigned long long* outSessionID)
{
	if (outIndex != nullptr) *outIndex = sessionHandle >> 48;
	if (outSessionID != nullptr) *outSessionID = sessionHandle & 0x0000ffffffffffff;
}

void NetLib_Server::DisconnectAll()
{
	int target = m_sessionCount;
	for (int i = 0; i < target; i++)
	{
		Session* session = &m_sessions[i];
		Disconnect(session->SessionHandle);
	}
}

void NetLib_Server::RegistContent(NetLib_Content* content)
{
	//if (m_frequencyThread == 0)
	//{
	//	m_frequencyThread = (HANDLE)_beginthreadex(0, 0, SchedulerThread, this, 0, 0);
	//}

	PostQueuedCompletionStatus(m_iocpHandle, NetLib_Content::REQUEST_CONTENT_BEGIN,
		(ULONG_PTR)content, (WSAOVERLAPPED*)content->overlapped_content_begin);
}


/// <summary>

/// </summary>
/// <param name="argv"></param>
/// <returns></returns>
unsigned int __stdcall NetLib_Server::SchedulerThread(void* argv)
{
	// 몇 초뒤에 동작할 함수포인터를 등록하게 하는 편이 좋아보인다.
	NetLib_Server* server = (NetLib_Server*)argv;
	DWORD prevTime = timeGetTime();
	int tick = 1;
	while (1)
	{
		// 10ms마다 일어나서 순회돌고 Update를 워커에 Post
		DWORD delta = timeGetTime() - prevTime;
		prevTime += delta;
		int t = tick - delta;
		if (t > 0)
		{
			Sleep(t);
			delta += t;
			prevTime += t;
		}
		{
			//PROFILING("FrequencyLoop");
			AcquireSRWLockShared(&server->m_lock_registered_contents);
			for (auto itRegistered = server->m_registered_contents.begin(); itRegistered != server->m_registered_contents.end(); ++itRegistered)
			{
				NetLib_Content* content = itRegistered->second;
				DWORD now = timeGetTime();
				DWORD cd = now - content->m_lastUpdate;
				if (cd >= content->m_msFrequency)
				{
					if (InterlockedExchange8((char*)&content->IsUpdating, 1) == 0)
					{
						content->m_lastUpdate = now;
						content->m_lastDeltaTime = cd / 1000.0f;
						PostQueuedCompletionStatus(server->m_iocpHandle, NetLib_Content::REQUEST_CONTENT_UPDATE,
							(ULONG_PTR)content, (WSAOVERLAPPED*)content->overlapped_content_update);
					}
				}
			}
			ReleaseSRWLockShared(&server->m_lock_registered_contents);
		}

	}
	return 0;
}

bool NetLib_Server::Move_Content(NetLib_Content* content, unsigned long long sessionHandle, void* completionKey)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return false;

	if (session->m_content == content)
	{
		ReturnSession(session);
		return false;
	}
	InterlockedExchange8((char*)&session->IsMoving, 1);

	// 내가 컨텐츠가 있다면 Leave해놓고 넣어야함
	if (session->m_content != nullptr)
	{
		session->m_content->Leave(session);
	}
	else
	{
		InterlockedExchange8((char*)&session->IsMoving, 0);
	}

	InterlockedExchange64((long long*)&session->m_content, (long long)content);
	if (content != nullptr)
	{
		session->completionKey = completionKey;
		content->m_msgQueue.Enqueue({ 0, session });
	}
	else
	{
		ReturnSession(session);
	}
	return true;
}

float NetLib_Server::WaitForTime(int tick, DWORD* prevTime)
{
	DWORD delta = (timeGetTime() - *prevTime);
	*prevTime += delta;
	int t = tick - delta;
	if (t > 0)
	{
		Sleep(t);
		delta += t;
		*prevTime += t;

	}
	return delta / 1000.0f;
}