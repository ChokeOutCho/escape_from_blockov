#include <iostream>
#include "NetLib_Client.h"
#include "../Utils/Logger.h"
unsigned int __stdcall NetLib_Client::WorkerThread(void* argv)
{
	NetLib_Client* client = (NetLib_Client*)argv;
	//printf("워커스레드 시작 랜서버: %p\n", client);
	while (1)
	{
		DWORD transferred = 0;
		Session* session = 0;
		CUSTOM_OVERLAPPED* overlapped = 0;
		int gqcs_ret;

		gqcs_ret = GetQueuedCompletionStatus(client->m_iocpHandle, &transferred, (PULONG_PTR)&session,
			(LPOVERLAPPED*)&overlapped, INFINITE);
		// IO걸린상태에서 소켓끊으면 디큐실패
		//if (!gqcs_ret)
		//{
		//	if (transferred != 0)
		//	{
		//		int overlappedstatus = 0; /* 1: send / 2 : recv */
		//		if (overlapped == &session->overlapped_send)
		//			overlappedstatus = 1;
		//		else if (overlapped == &session->overlapped_recv)
		//			overlappedstatus = 2;

		//		LOG(L"GQCS", LEVEL_ERROR, L"GQCS failed but IO Success. SessionHandle: %d transferred: %d, IO: %d", transferred, session->SessionHandle, overlappedstatus);
		//	}

		//}

		// 종료 로직
		if (transferred == 0 && (__int64)session == 0 && (__int64)overlapped == 0)
			break;

		if (overlapped == session->overlapped_recv)
		{
			if (transferred == 0)
			{
				client->Decrement_IOCount(session);
				continue;
			}

			// 수신 완료 작업
			session->m_recvBuffer->MoveRear(transferred);

			while (1)
			{
				// 헤더 까보기
				char iobuf[sizeof(NetHeader) + PAYLOAD_LEN_DEFAULT];
				if (session->m_recvBuffer->GetUseSize() < sizeof(NetHeader)) break;
				NetHeader* header = (NetHeader*)iobuf;
				char* payload = iobuf + sizeof(NetHeader);
				int ret_peek = session->m_recvBuffer->Peek((char*)header, sizeof(NetHeader));

				if (header->Code != client->m_header_code)
				{
					client->Disconnect();
					break;
				}

				if (header->Len > PAYLOAD_LEN_DEFAULT)
				{
					client->Disconnect();
					break;
				}

				// 데이터 있으면 패킷으로 카피
				if (session->m_recvBuffer->GetUseSize() < sizeof(NetHeader) + header->Len) break;
				session->m_recvBuffer->MoveFront(sizeof(NetHeader));

				// 바이트스트림 링버퍼라 카피떠야 한번에 빼기가 편함
				int ret_deq = session->m_recvBuffer->Dequeue(payload, header->Len);

				if (client->m_opt_encryption)
				{

					// 페이로드 디코딩
					SimpleEncoder decoder;
					decoder.SetBuffers(&header->CheckSum, &header->CheckSum, header->Len + 1);
					decoder.SetKeys(client->m_fixed_key, header->RandKey);
					decoder.Decode();
					unsigned char checkSum = decoder.CalculateChecksum((unsigned char*)payload, header->Len);

					if (checkSum != header->CheckSum)
					{
						//printf("체크섬 다름?????\n");
						client->Disconnect();
						break;
					}

				}

				Packet* packet = Packet::Alloc();
				//packet->type = Packet::eType::RECVPROC;

				packet->PutData(payload, header->Len);
				session->cumulative_recv++;
				client->OnRecv(packet);
				Packet::Free(packet);
			}

			if (client->Increment_IOCount(session) == true)
			{
				continue;
			}
			// 다음을 위한 리시브
			if (session->RecvPost() == false)
			{
				client->Decrement_IOCount(session);
			}
			else
			{
				if (session->requestDisconnect == true)
				{
					CancelIoEx((HANDLE)session->m_socket, nullptr);
				}
			}


		} // end if(recv)

		else if (overlapped == session->overlapped_send)
		{
			if (transferred == 0)
			{
				client->Decrement_IOCount(session);
				continue;
			}


			if (transferred != Session::REQUEST_SENDING_CHECK)
				session->ClearSendPackets();


			InterlockedExchange8(&session->IsSending, 0);
			if (session->m_sendBuffer.GetUseSize() > 0)
			{
				if (client->Increment_IOCount(session) == false)
				{
					if (session->TrySendPost() == false)
					{
						client->Decrement_IOCount(session);

					}

				}

			}

		}// end if(send)
		else if (overlapped == session->overlapped_sendpost)
		{
			if (session->SendPost() == true)
			{
			}
			else
			{
				client->Decrement_IOCount(session);
			}
			continue;
			}

		else if (overlapped == session->overlapped_leave)
		{
			client->m_Connect = 0;
			client->OnLeaveServer();
			continue;
		}

		client->Decrement_IOCount(session);

	}

	printf("워커 스레드 종료\n");
	return 0;
}


unsigned int __stdcall NetLib_Client::MonitorThread(void* argv)
{
	NetLib_Client* client = (NetLib_Client*)argv;
	// TPS 측정
	// 세션 TPS를 모아둘듯
	// 그냥 대략 측정하려고 동기화를 걸진 않음. 경합이 많이나면 몇개는 씹힐 수 있음. 신경안씀.
	// 세션이 가지고 있는 TPS를 취합하기 때문에 연결이 끊긴 세션과의 TPS는 사라짐.
	while (client->m_Connect)
	{
		int sum_recv = 0;
		int sum_send = 0;
		InterlockedExchange((long*)&client->m_tps_recv, 0);
		InterlockedExchange((long*)&client->m_tps_send, 0);

		for (int i = 0; i < client->m_opt_maxOfSession; i++)
		{
			Session* session = &client->m_server_session;
			if (session->m_IsConnecting == 1)
			{
				session->tps_recv = session->cumulative_recv;
				session->cumulative_recv = 0;
				session->tps_send = session->cumulative_send;
				session->cumulative_send = 0;

				sum_recv += session->tps_recv;
				sum_send += session->tps_send;

			}
		}

		// Release된 세션의 TPS도 더해주기
		int size = client->m_tpsSets.GetUseSize();
		for (int i = 0; i < size; i++)
		{
			TPS_SET ret;
			client->m_tpsSets.Dequeue(ret);
			sum_send += ret.send;
			sum_recv += ret.recv;
		}
		InterlockedAdd((long*)&client->m_tps_recv, sum_recv);
		InterlockedAdd((long*)&client->m_tps_send, sum_send);


		//server->m_tps_recv += sum_recv;
		//server->m_tps_send += sum_send;

		Sleep(1000);

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
bool NetLib_Client::SendPacket(Packet* packet)
{
	Session* session = FindSession(m_server_session.SessionHandle);
	if (session == nullptr)
		return false;

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

	// 사이즈 미정
	//if (enqueueSize != sizeof(Packet*))
	//{
	//	printf("\n\n\n\n\n\n\n\n넘쳤으면 죽어\n\n\n\n\n\n");
	//	Decrement_IOCount(session);
	//	return false;
	//}
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


bool NetLib_Client::Disconnect()
{
	Session* session = FindSession(m_server_session.SessionHandle);
	if (session == nullptr)
		return false;
	//printf("끊기요청0x%llx\n", sessionHandle);
	InterlockedExchange8((char*)&session->requestDisconnect, 1);
	CancelIoEx((HANDLE)session->m_socket, nullptr);
	ReturnSession(session);
	return true;
}

NetLib_Client::NetLib_Client(const WCHAR* connectIP, unsigned short connectPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, bool opt_zerocpy, bool opt_use_monitor, Opt_Encryption* opt_encryption)
{
	m_errCode = 0;
	m_idPool = 0;
	m_monitorThread = 0;
	m_server_socket = 0;
	m_Connect = 0;
	m_tps_recv = 0;
	m_tps_send = 0;
	m_opt_workerTH_count = opt_workerTH_Pool_size;
	m_opt_concurrentTH_size = opt_concurrentTH_size;
	ZeroMemory(&m_server_addr, sizeof(SOCKADDR_IN));
	unsigned long ip;
	InetPtonW(AF_INET, connectIP, &m_server_addr.sin_addr);
	m_server_addr.sin_family = AF_INET;
	//m_server_addr.sin_addr.s_addr = htonl(ip);
	m_server_addr.sin_port = htons(connectPort);
	m_opt_zerocpy = opt_zerocpy;
	// opt_encryption == nullptr: 인코딩/체크섬 미사용. 헤더 Code는 기본값(0x77)
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

NetLib_Client::~NetLib_Client()
{
	Disconnect();
}


bool NetLib_Client::Connect()
{
	m_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(m_server_socket, (sockaddr*)&m_server_addr, sizeof(m_server_addr)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(m_server_socket);
		return false;
	}
	if (m_opt_monitor == true)
		m_monitorThread = (HANDLE)_beginthreadex(0, 0, MonitorThread, this, 0, 0);
	m_Connect = 1;
	if (m_opt_zerocpy)
	{
		int size = 0;
		setsockopt(m_server_socket, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size));

	}

	unsigned long long allocID = InterlockedIncrement64((long long*)&m_idPool);
	m_server_session.Connect(allocID, m_server_socket, m_iocpHandle, 0, 0);
	CreateIoCompletionPort((HANDLE)m_server_socket, m_iocpHandle, (unsigned long long)&m_server_session, 0);

	OnEnterJoinServer();
	if (m_server_session.RecvPost() == true)
	{

	}
	else
	{
		if (Decrement_IOCount(&m_server_session) == true)
		{
			//printf("aa\n");
			//printf("recv 릴리즈에 늦게들어온 놈이 CAS 자체를 재사용 뒤에 하면서 새로 연결된 세션이 끊어질 가능성이 있다. %d\n", ++server->m_disconnectCount);

		}

	}

	return true;
}

void NetLib_Client::Stop()
{
	m_Connect = 0;

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

}

__inline Session* NetLib_Client::FindSession(unsigned long long sessionHandle)
{
	unsigned short index;
	DecodeSessionHandle(sessionHandle, &index, nullptr);
	Session* session = &m_server_session;

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
bool NetLib_Client::Increment_IOCount(Session* session)
{
	unsigned short currIO = InterlockedIncrement16((short*)&session->IOCount);
	unsigned char flag;
	unsigned char ioCount;
	Session::DecodeIOCount(currIO, flag, ioCount);

	return flag;
	//return false;
}

bool NetLib_Client::Decrement_IOCount(Session* session)
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

void NetLib_Client::ReleaseSession(Session* session)
{
	if (InterlockedCompareExchange16((short*)&session->IOCount, Session::RELEASE_IOCOUNT, 0) != 0)
	{
		//printf("중복해제감지");
		return;
	}
	//m_tps_recv += session->cumulative_recv;
	//m_tps_send += session->cumulative_send; 
	InterlockedExchange8((char*)&session->m_IsConnecting, 0);

	closesocket(session->m_socket);
	session->m_socket = INVALID_SOCKET;
	session->ClearSendPackets();
	session->ClearSendBuffer();
	InterlockedExchange8(&session->IsSending, 0);
	m_tpsSets.Enqueue({ session->cumulative_send, session->cumulative_recv });

	//OnClientLeave(session->SessionHandle);
	//printf("\nSession Diconnect  SessionHandle: 0x%16llx\n", session->SessionHandle);
	PostQueuedCompletionStatus(m_iocpHandle, 0, (ULONG_PTR)session, (WSAOVERLAPPED*) session->overlapped_leave);
}

int NetLib_Client::GetSessionTPS_Send(unsigned long long sessionHandle)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return 0;

	int ret = session->tps_send;

	ReturnSession(session);
	return ret;
}
int NetLib_Client::GetSessionTPS_Recv(unsigned long long sessionHandle)
{
	Session* session = FindSession(sessionHandle);
	if (session == nullptr)
		return 0;

	int ret = session->tps_recv;
	ReturnSession(session);
	return ret;
}

__inline void NetLib_Client::CreateSessionHandle(unsigned short index, unsigned long long sessionID, unsigned long long* outSessionHandle)
{
	unsigned long long IDUpper = index;
	IDUpper = IDUpper << 48;

	unsigned long long IDLower = sessionID;
	IDLower = IDLower & 0x0000ffffffffffff;

	*outSessionHandle = IDUpper | IDLower;
}

__inline void NetLib_Client::DecodeSessionHandle(unsigned long long sessionHandle, unsigned short* outIndex, unsigned long long* outSessionID)
{
	if (outIndex != nullptr) *outIndex = sessionHandle >> 48;
	if (outSessionID != nullptr) *outSessionID = sessionHandle & 0x0000ffffffffffff;
}
