#include "NetLib_Content.h"

void NetLib_Content::Update(float deltaTime)
{

	while (1)	// 큐털기
	{
		ContentMSG msg;
		if (m_msgQueue.Dequeue(msg) == -1)
			break;
		Session* session = (Session*)msg.session;
		switch (msg.type)
		{
		case 0: // ENTER
		{
			// 날리지 말고 프로토콜로 거르게 하자.
			//session->ClearContentQueue();
			m_sessionCount++;
			sessions[session->SessionHandle] = session;
			OnEnter(session->SessionHandle, session->completionKey);
			m_server->ReturnSession(session);
			break;
		}
		case 1: // LEAVE
		{
			sessions.erase(session->SessionHandle);
			InterlockedExchange8((char*)&session->IsMoving, 0);
			m_sessionCount--;
			break;
		}

		case 2: // RELEASE
		{
			// 여기서 컨텐츠큐정리하고 세션 반환하면 아래에서 해당 세션을 참조할 일 없음
			Release(session);
			break;
		}
		default:
			break;
		}
	}

	// 세션 돌며 recv
	for (auto it = sessions.begin(); it != sessions.end(); ++it)
	{
		unsigned long long sessionHandle = it->first;
		//Session* session = it->second; // 세션을 획득해두고 내부 send에서는 획득하지않기
		//if (m_server->Increment_IOCount(session) == true)
		//	continue;
		Session* session = m_server->FindSession(sessionHandle);
		if (session == nullptr) continue;
		RingBuffer* currRecvBuffer = session->m_recvrecvBuffer;
		while (1)
		{

			if (session->IsMoving == true)
				break;

			int bufferlen = currRecvBuffer->GetUseSize();
			char serializeBuf[sizeof(ContentQueueHeader) + PAYLOAD_LEN_DEFAULT];
			if (bufferlen < sizeof(ContentQueueHeader)) break;
			ContentQueueHeader* header = (ContentQueueHeader*)serializeBuf;
			int ret_peek = currRecvBuffer->Peek((char*)header, sizeof(ContentQueueHeader));
			if (bufferlen < sizeof(ContentQueueHeader) + header->Len) break;
			currRecvBuffer->MoveFront(sizeof(ContentQueueHeader));
			char* payload = serializeBuf + sizeof(ContentQueueHeader);
			//int ret_deq = 
			currRecvBuffer->Dequeue(payload, header->Len);
			//if (ret_deq != header->Len)
			//{
			//	// 링 버퍼 결함
			//	printf("\n링 버퍼 결함 발생. recv deq");
			//	DebugBreak();
			//}
			OnRecv(sessionHandle, payload, header->Len);
			//Packet* packet;
			//	if (session->m_contentQueue.Dequeue(packet) == -1)
			//	break;
			//OnRecv(sessionHandle, packet);
			//Packet::Free(packet);
		}
		//m_server->Decrement_IOCount(session);
		m_server->ReturnSession(session);
	}

	// OnUpdate
	OnUpdate(deltaTime);

	//InterlockedExchange8((char*)&IsUpdating, 0);

}


void NetLib_Content::Release(Session* session)
{
	sessions.erase(session->SessionHandle);
	m_sessionCount--;
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

	m_server->m_tpsSets.Enqueue({ session->cumulative_send, session->cumulative_recv });

	OnRelease(session->SessionHandle, session->m_leave_code, session->IP, session->Port);

	unsigned short index;
	m_server->DecodeSessionHandle(session->SessionHandle, &index, nullptr);
	m_server->m_sessionIndexPool.Push(index);
	InterlockedDecrement(&m_server->m_sessionCount);
}

void NetLib_Content::Leave(Session* session)
{
	OnLeave(session->SessionHandle);
	m_msgQueue.Enqueue({ 1, session });
}