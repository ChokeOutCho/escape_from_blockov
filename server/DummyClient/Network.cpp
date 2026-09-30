#include "Network.h"
#include "Dummy.h"
#include <process.h>

Network g_net;

bool Network::Start(int ioThreads, const std::string& ip, int port, std::string& err)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { err = "WSAStartup failed"; return false; }

	m_addr.sin_family = AF_INET;
	m_addr.sin_port = htons((u_short)port);
	if (inet_pton(AF_INET, ip.c_str(), &m_addr.sin_addr) != 1)
	{
		// 호스트 이름이면 조회
		addrinfo hints{}, * res = nullptr;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(ip.c_str(), nullptr, &hints, &res) != 0 || !res) { err = "invalid server address: " + ip; return false; }
		m_addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
		freeaddrinfo(res);
	}

	// ConnectEx 함수 포인터
	SOCKET tmp = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	GUID guid = WSAID_CONNECTEX;
	DWORD bytes = 0;
	int r = WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &m_connectEx, sizeof(m_connectEx), &bytes, nullptr, nullptr);
	closesocket(tmp);
	if (r == SOCKET_ERROR || !m_connectEx) { err = "ConnectEx not available"; return false; }

	m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (!m_iocp) { err = "CreateIoCompletionPort failed"; return false; }
	for (int i = 0; i < ioThreads; i++)
		m_threads.push_back((HANDLE)_beginthreadex(nullptr, 0, WorkerThread, this, 0, nullptr));
	return true;
}

void Network::Stop()
{
	for (size_t i = 0; i < m_threads.size(); i++)
		PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr);
	if (!m_threads.empty())
		WaitForMultipleObjects((DWORD)m_threads.size(), m_threads.data(), TRUE, 5000);
	for (HANDLE h : m_threads) CloseHandle(h);
	m_threads.clear();
	if (m_iocp) CloseHandle(m_iocp);
	m_iocp = nullptr;
	WSACleanup();
}

bool Network::Connect(Dummy* d, uint32_t now)
{
	if (d->net != NetState::Idle) return false;

	SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET) { g_stats.connectFail++; return false; }

	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = 0;
	BOOL on = TRUE;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
	if (bind(s, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR ||
		CreateIoCompletionPort((HANDLE)s, m_iocp, (ULONG_PTR)d, 0) == nullptr)
	{
		closesocket(s);
		g_stats.connectFail++;
		return false;
	}

	d->sock = s;
	d->net = NetState::Connecting;
	d->closeIntended = false;
	d->recvLen = 0;
	d->sendQ.clear();
	d->sending = false;

	memset(&d->connCtx.ov, 0, sizeof(OVERLAPPED));
	d->connCtx.type = IO_CONNECT;
	d->ioCount++;
	if (!m_connectEx(s, (sockaddr*)&m_addr, sizeof(m_addr), nullptr, 0, nullptr, &d->connCtx.ov))
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			d->ioCount--;
			g_stats.connectFail++;
			Close(d, false, now);
			return false;
		}
	}
	return true;
}

void Network::Send(Dummy* d, const char* data, int len)
{
	if (d->net != NetState::Connected) return;
	if (d->sendQ.size() > 256 * 1024)
	{
		// 서버가 받지 않고 있음 → 끊는다
		Close(d, false, NowMs());
		return;
	}
	d->sendQ.insert(d->sendQ.end(), data, data + len);
	g_stats.sendPkts++;
	if (!d->sending) PostSend(d, NowMs());
}

void Network::Close(Dummy* d, bool intended, uint32_t now)
{
	if (d->net == NetState::Idle || d->net == NetState::Closing) return;
	d->net = NetState::Closing;
	d->closeIntended = d->closeIntended || intended;
	if (d->sock != INVALID_SOCKET)
	{
		LINGER lg{ 1, 0 };     // abortive close: 클라 쪽 TIME_WAIT 을 남기지 않는다 (대량 재접속 시 포트 고갈 방지)
		setsockopt(d->sock, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
		closesocket(d->sock);
		d->sock = INVALID_SOCKET;
	}
	if (d->ioCount == 0) FinishClose(d, now);
}

void Network::FinishClose(Dummy* d, uint32_t now)
{
	d->net = NetState::Idle;
	d->recvLen = 0;
	d->sendQ.clear();
	d->sendInflight.clear();
	d->sending = false;
	g_stats.disconnects++;
	d->OnClosed(now, d->closeIntended);
}

void Network::PostRecv(Dummy* d, uint32_t now)
{
	WSABUF buf;
	buf.buf = d->recvBuf + d->recvLen;
	buf.len = (ULONG)(Dummy::RECV_BUF - d->recvLen);
	DWORD flags = 0;
	memset(&d->recvCtx.ov, 0, sizeof(OVERLAPPED));
	d->recvCtx.type = IO_RECV;
	d->ioCount++;
	if (WSARecv(d->sock, &buf, 1, nullptr, &flags, &d->recvCtx.ov, nullptr) == SOCKET_ERROR &&
		WSAGetLastError() != WSA_IO_PENDING)
	{
		d->ioCount--;
		Close(d, false, now);
	}
}

void Network::PostSend(Dummy* d, uint32_t now)
{
	if (d->sendQ.empty() || d->net != NetState::Connected) return;
	d->sendInflight.swap(d->sendQ);
	d->sendQ.clear();
	WSABUF buf;
	buf.buf = d->sendInflight.data();
	buf.len = (ULONG)d->sendInflight.size();
	memset(&d->sendCtx.ov, 0, sizeof(OVERLAPPED));
	d->sendCtx.type = IO_SEND;
	d->sending = true;
	d->ioCount++;
	if (WSASend(d->sock, &buf, 1, nullptr, 0, &d->sendCtx.ov, nullptr) == SOCKET_ERROR &&
		WSAGetLastError() != WSA_IO_PENDING)
	{
		d->ioCount--;
		d->sending = false;
		Close(d, false, now);
	}
}

void Network::ProcessRecv(Dummy* d, uint32_t now)
{
	int pos = 0;
	while (d->net == NetState::Connected && d->recvLen - pos >= NET_HEADER_SIZE)
	{
		const uint8_t* h = (const uint8_t*)d->recvBuf + pos;
		uint16_t len = (uint16_t)(h[1] | (h[2] << 8));
		if (h[0] != NET_HEADER_CODE || len < 2 || len > NET_MAX_PAYLOAD)
		{
			Close(d, false, now);   // 프로토콜 깨짐
			return;
		}
		if (d->recvLen - pos < NET_HEADER_SIZE + len) break;
		g_stats.recvPkts++;
		d->OnPacket(h + NET_HEADER_SIZE, len, now);
		pos += NET_HEADER_SIZE + len;
	}
	if (pos > 0 && d->net == NetState::Connected)
	{
		memmove(d->recvBuf, d->recvBuf + pos, d->recvLen - pos);
		d->recvLen -= pos;
	}
}

unsigned __stdcall Network::WorkerThread(void* arg)
{
	((Network*)arg)->Worker();
	return 0;
}

void Network::Worker()
{
	for (;;)
	{
		DWORD bytes = 0;
		ULONG_PTR key = 0;
		OVERLAPPED* ov = nullptr;
		BOOL ok = GetQueuedCompletionStatus(m_iocp, &bytes, &key, &ov, INFINITE);
		if (key == 0 && ov == nullptr) break;      // Stop()
		if (ov == nullptr) continue;

		Dummy* d = (Dummy*)key;
		IoCtx* ctx = CONTAINING_RECORD(ov, IoCtx, ov);
		uint32_t now = NowMs();

		AcquireSRWLockExclusive(&d->lock);
		switch (ctx->type)
		{
		case IO_CONNECT:
			if (ok && d->net == NetState::Connecting)
			{
				setsockopt(d->sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
				d->net = NetState::Connected;
				g_stats.connectOk++;
				d->OnConnected(now);
				if (d->net == NetState::Connected) PostRecv(d, now);
			}
			else
			{
				if (d->net == NetState::Connecting) g_stats.connectFail++;
				Close(d, false, now);
			}
			break;

		case IO_RECV:
			if (!ok || bytes == 0 || d->net != NetState::Connected)
			{
				Close(d, false, now);
				break;
			}
			g_stats.recvBytes += bytes;
			d->recvLen += (int)bytes;
			ProcessRecv(d, now);
			if (d->net == NetState::Connected) PostRecv(d, now);
			break;

		case IO_SEND:
			d->sending = false;
			if (!ok || d->net != NetState::Connected)
			{
				Close(d, false, now);
				break;
			}
			g_stats.sendBytes += bytes;
			d->sendInflight.clear();
			if (!d->sendQ.empty()) PostSend(d, now);
			break;
		}

		if (--d->ioCount == 0 && d->net == NetState::Closing)
			FinishClose(d, now);
		ReleaseSRWLockExclusive(&d->lock);
	}
}
