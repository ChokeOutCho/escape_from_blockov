// [테스트 전용] StubNetLib 구현 (리눅스, 단일 스레드 poll 루프)
#include "StubNetLib.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>

NetLib_Server::NetLib_Server(const WCHAR*, unsigned short openPort, int, int, int, bool, Opt_Encryption*, int)
	: m_port(openPort)
{
}

void NetLib_Server::Start()
{
	m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(m_port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(m_listenFd, (sockaddr*)&a, sizeof(a)) != 0) { perror("bind"); return; }
	listen(m_listenFd, 128);
	fcntl(m_listenFd, F_SETFL, O_NONBLOCK);
	m_running = true;
	m_thread = std::thread([this] { Loop(); });
}

void NetLib_Server::RegistContent(NetLib_Content* c)
{
	c->m_server = this;
	m_contents.push_back(c);
}

NetLib_Server::Session* NetLib_Server::Find(unsigned long long h)
{
	auto it = m_sessions.find(h);
	if (it == m_sessions.end() || it->second.closing) return nullptr;
	return &it->second;
}

static void AppendPacket(std::vector<char>& out, const char* payload, int len)
{
	char hdr[5] = { 0x77, (char)(len & 0xFF), (char)((len >> 8) & 0xFF), 0, 0 };
	out.insert(out.end(), hdr, hdr + 5);
	out.insert(out.end(), payload, payload + len);
}

bool NetLib_Server::SendPacket(unsigned long long h, Packet* p)
{
	Session* s = Find(h);
	if (!s) return false;
	AppendPacket(s->out, p->GetBufferPtr(), p->GetDataSize());
	m_cSend++;
	m_bSend += 5 + p->GetDataSize();
	return true;
}

bool NetLib_Server::SendPacketMulticast(unsigned long long* hs, long n, Packet* p)
{
	bool ok = true;
	for (long i = 0; i < n; i++) ok = SendPacket(hs[i], p) && ok;
	return ok;
}

bool NetLib_Server::SendPacketFast(unsigned long long h, Packet* p)
{
	bool ok = SendPacket(h, p);
	Packet::Free(p);
	return ok;
}

bool NetLib_Server::Disconnect(unsigned long long h)
{
	Session* s = Find(h);
	if (!s) return false;
	FlushSession(*s);
	CloseSession(*s);
	return true;
}

bool NetLib_Server::Move_Content(NetLib_Content* c, unsigned long long h, void* key)
{
	Session* s = Find(h);
	if (!s || s->content == c) return false;
	if (s->content)
	{
		auto& v = s->content->m_sessions;
		v.erase(std::remove(v.begin(), v.end(), h), v.end());
		s->content->OnLeave(h);
	}
	s->content = c;
	if (c) c->m_msgs.push_back({ 0, h, key });
	return true;
}

void NetLib_Server::CloseSession(Session& s)
{
	if (s.closing) return;
	s.closing = true;
	close(s.fd);
	m_sessionCount--;
	if (s.content) s.content->m_msgs.push_back({ 2, s.handle, nullptr });
	else { OnClientLeave(s.handle, NONE, 0, 0); s.released = true; }
}

void NetLib_Server::Accept()
{
	while (true)
	{
		sockaddr_in a{};
		socklen_t l = sizeof(a);
		int fd = accept(m_listenFd, (sockaddr*)&a, &l);
		if (fd < 0) return;
		fcntl(fd, F_SETFL, O_NONBLOCK);
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		unsigned long ip = ntohl(a.sin_addr.s_addr);
		unsigned short port = ntohs(a.sin_port);
		if (!OnConnectionRequest(ip, port)) { close(fd); continue; }
		unsigned long long h = m_nextHandle++;
		Session& s = m_sessions[h];
		s.fd = fd;
		s.handle = h;
		m_sessionCount++;
		OnClientJoin(h, ip, port);
	}
}

void NetLib_Server::ReadSession(Session& s)
{
	char buf[4096];
	while (true)
	{
		ssize_t n = recv(s.fd, buf, sizeof(buf), 0);
		if (n > 0) { s.in.insert(s.in.end(), buf, buf + n); m_bRecv += n; }
		else if (n == 0) { CloseSession(s); return; }
		else break;
	}
	size_t pos = 0;
	while (s.in.size() - pos >= 5)
	{
		unsigned char code = (unsigned char)s.in[pos];
		int len = (unsigned char)s.in[pos + 1] | ((unsigned char)s.in[pos + 2] << 8);
		if (code != 0x77 || len > PAYLOAD_LEN_DEFAULT) { printf("[stub] bad header, close\n"); CloseSession(s); return; }
		if (s.in.size() - pos < (size_t)(5 + len)) break;
		const char* payload = s.in.data() + pos + 5;
		m_cRecv++;
		if (s.content) s.payloads.emplace_back(payload, payload + len);
		else
		{
			Packet* p = Packet::Alloc();
			for (int i = 0; i < len; i++) *p << (char)payload[i];
			OnRecv(s.handle, p);
			Packet::Free(p);
		}
		pos += 5 + len;
	}
	s.in.erase(s.in.begin(), s.in.begin() + pos);
}

void NetLib_Server::FlushSession(Session& s)
{
	while (!s.out.empty())
	{
		ssize_t n = send(s.fd, s.out.data(), s.out.size(), MSG_NOSIGNAL);
		if (n <= 0) break;
		s.out.erase(s.out.begin(), s.out.begin() + n);
	}
}

void NetLib_Server::UpdateContent(NetLib_Content* c, DWORD now)
{
	while (!c->m_msgs.empty())
	{
		auto m = c->m_msgs.front();
		c->m_msgs.pop_front();
		if (m.type == 0)
		{
			c->m_sessions.push_back(m.h);
			c->OnEnter(m.h, m.key);
		}
		else
		{
			auto& v = c->m_sessions;
			v.erase(std::remove(v.begin(), v.end(), m.h), v.end());
			auto it = m_sessions.find(m.h);
			c->OnRelease(m.h, NONE, 0, 0);
			if (it != m_sessions.end()) it->second.released = true;
		}
	}
	std::vector<unsigned long long> hs = c->m_sessions;
	for (auto h : hs)
	{
		auto it = m_sessions.find(h);
		if (it == m_sessions.end()) continue;
		Session& s = it->second;
		while (!s.payloads.empty() && s.content == c)
		{
			std::vector<char> pl = std::move(s.payloads.front());
			s.payloads.pop_front();
			c->OnRecv(h, pl.data(), (int)pl.size());
		}
	}
	c->OnUpdate(c->m_msFrequency / 1000.0f);
}

void NetLib_Server::Loop()
{
	for (auto* c : m_contents) { c->m_lastUpdate = timeGetTime(); c->OnBegin(); }
	while (m_running)
	{
		std::vector<pollfd> fds;
		std::vector<unsigned long long> order;
		fds.push_back({ m_listenFd, POLLIN, 0 });
		for (auto& kv : m_sessions)
		{
			if (kv.second.closing) continue;
			short ev = POLLIN | (kv.second.out.empty() ? 0 : POLLOUT);
			fds.push_back({ kv.second.fd, ev, 0 });
			order.push_back(kv.first);
		}
		poll(fds.data(), fds.size(), 2);
		if (fds[0].revents & POLLIN) Accept();
		for (size_t i = 1; i < fds.size(); i++)
		{
			auto it = m_sessions.find(order[i - 1]);
			if (it == m_sessions.end() || it->second.closing) continue;
			if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) ReadSession(it->second);
		}
		DWORD now = timeGetTime();
		if (now - m_lastStat >= 1000)
		{
			m_lastStat = now;
			m_tpsRecv = m_cRecv; m_tpsSend = m_cSend; m_bpsRecv = m_bRecv; m_bpsSend = m_bSend;
			m_cRecv = m_cSend = 0; m_bRecv = m_bSend = 0;
		}
		for (auto* c : m_contents)
		{
			while ((int)(now - c->m_lastUpdate) >= c->m_msFrequency)
			{
				c->m_lastUpdate += c->m_msFrequency;
				UpdateContent(c, now);
			}
		}
		for (auto& kv : m_sessions)
			if (!kv.second.closing) FlushSession(kv.second);
		for (auto it = m_sessions.begin(); it != m_sessions.end();)
		{
			if (it->second.closing && it->second.released) it = m_sessions.erase(it);
			else ++it;
		}
	}
}
