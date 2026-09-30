#pragma once
////////////////////////////////////////////////////////////////////////
// [테스트 전용] 리눅스용 NetLib API 스텁.
// 실제 NetLib와 같은 클래스/시그니처를 제공하되, 단일 스레드 poll() 루프로 동작한다.
// 목적: 게임 로직(Entry/BattleContent)을 리눅스에서 컴파일·실행해 봇으로 검증.
// 와이어 형식은 실제와 동일: | Code(0x77) | Len(2) | RandKey(0) | CheckSum(0) | Payload |
////////////////////////////////////////////////////////////////////////
#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef wchar_t WCHAR;

inline DWORD timeGetTime()
{
	using namespace std::chrono;
	return (DWORD)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
inline void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#define PAYLOAD_LEN_DEFAULT 512

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

struct Opt_Encryption { char Header_Code = 0x77; char Fixed_Key = 0x32; };

class Packet
{
public:
	static Packet* Alloc() { Packet* p = new Packet(); p->m_net = false; return p; }
	static Packet* NetAlloc() { Packet* p = new Packet(); p->m_net = true; return p; }
	static void Free(Packet* p) { delete p; }
	static int GetPoolUseSize() { return s_live.load(); }

	Packet& operator<<(BYTE v) { return Put(v); }
	Packet& operator<<(char v) { return Put(v); }
	Packet& operator<<(WORD v) { return Put(v); }
	Packet& operator<<(short v) { return Put(v); }
	Packet& operator<<(int v) { return Put(v); }
	Packet& operator<<(DWORD v) { return Put(v); }
	Packet& operator<<(float v) { return Put(v); }
	Packet& operator<<(int64_t v) { return Put(v); }

	int GetDataSize() const { return (int)m_buf.size(); }
	char* GetBufferPtr() { return m_buf.data(); }
	bool IsNet() const { return m_net; }

private:
	Packet() { s_live++; }
	~Packet() { s_live--; }
	template <typename T> Packet& Put(T v)
	{
		if (m_buf.size() + sizeof(T) > PAYLOAD_LEN_DEFAULT) return *this;   // 실제와 같이 무음 실패
		size_t o = m_buf.size();
		m_buf.resize(o + sizeof(T));
		memcpy(m_buf.data() + o, &v, sizeof(T));
		return *this;
	}
	std::vector<char> m_buf;
	bool m_net = false;
	inline static std::atomic<int> s_live{ 0 };
};

class NetLib_Server;

class NetLib_Content
{
public:
	NetLib_Content(int msFrequency) : m_msFrequency(msFrequency) {}
	virtual ~NetLib_Content() {}

	virtual void OnBegin() {}
	virtual void OnEnd() {}
	virtual void OnUpdate(float deltaTime) {}
	virtual void OnRecv(unsigned long long sessionHandle, char* payload) {}
	virtual void OnRecv(unsigned long long sessionHandle, char* payload, int payloadLen) { OnRecv(sessionHandle, payload); }
	virtual void OnEnter(unsigned long long sessionHandle, void* completionKey) {}
	virtual void OnLeave(unsigned long long sessionHandle) {}
	virtual void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) {}

	long GetSessionCount() { return (long)m_sessions.size(); }
	int GetTick() { return m_msFrequency; }

	NetLib_Server* m_server = nullptr;

private:
	friend class NetLib_Server;
	struct Msg { int type; unsigned long long h; void* key; };   // 0 ENTER, 2 RELEASE
	int m_msFrequency;
	DWORD m_lastUpdate = 0;
	std::deque<Msg> m_msgs;
	std::vector<unsigned long long> m_sessions;
};

class NetLib_Server
{
public:
	NetLib_Server(const WCHAR* openIP, unsigned short openPort, int workers, int concurrent, int maxSession,
	              bool zeroCopy, Opt_Encryption* enc, int maxSendPackets);
	virtual ~NetLib_Server() {}

	void Start();
	void Stop() { m_running = false; if (m_thread.joinable()) m_thread.join(); }

	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port) { return false; }
	virtual void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port) {}
	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) {}
	virtual void OnRecv(unsigned long long sessionHandle, Packet* packet) {}

	bool SendPacket(unsigned long long h, Packet* packet);              // Alloc, 호출자 Free
	bool SendPacketMulticast(unsigned long long* hs, long n, Packet* packet);   // Alloc, 호출자 Free
	bool SendPacketFast(unsigned long long h, Packet* packet);          // NetAlloc, 소유권 이전
	bool SendPacketFastWithoutIOCount(unsigned long long h, Packet* packet) { return SendPacketFast(h, packet); }
	bool Disconnect(unsigned long long h);

	void RegistContent(NetLib_Content* content);
	bool Move_Content(NetLib_Content* content, unsigned long long h, void* completionKey = nullptr);

	int GetSessionCount() { return m_sessionCount.load(); }
	int GetPacketUseSize() { return Packet::GetPoolUseSize(); }
	int GetTPS_Recv() { return m_tpsRecv; }
	int GetTPS_Send() { return m_tpsSend; }
	int GetTPS_Accept() { return 0; }
	long long GetBPS_Recv() { return m_bpsRecv; }
	long long GetBPS_Send() { return m_bpsSend; }

private:
	struct Session
	{
		int fd;
		unsigned long long handle;
		NetLib_Content* content = nullptr;
		std::vector<char> in;
		std::vector<char> out;
		std::deque<std::vector<char>> payloads;
		bool closing = false;
		bool released = false;
	};

	void Loop();
	void Accept();
	void ReadSession(Session& s);
	void FlushSession(Session& s);
	void CloseSession(Session& s);
	void UpdateContent(NetLib_Content* c, DWORD now);
	void Enqueue(unsigned long long h, const char* payload, int len);
	Session* Find(unsigned long long h);

	unsigned short m_port;
	int m_listenFd = -1;
	std::atomic<bool> m_running{ false };
	std::atomic<int> m_sessionCount{ 0 };
	std::thread m_thread;
	unsigned long long m_nextHandle = 1;
	std::unordered_map<unsigned long long, Session> m_sessions;
	std::vector<NetLib_Content*> m_contents;
	std::atomic<int> m_tpsRecv{ 0 }, m_tpsSend{ 0 };
	std::atomic<long long> m_bpsRecv{ 0 }, m_bpsSend{ 0 };
	int m_cRecv = 0, m_cSend = 0;
	long long m_bRecv = 0, m_bSend = 0;
	DWORD m_lastStat = 0;
};
