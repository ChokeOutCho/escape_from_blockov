#pragma once
////////////////////////////////////////////////////////////////////////
// 다중 세션 IOCP 클라이언트 엔진
//  - 연결 1개 = Dummy 1개 (completion key = Dummy*)
//  - ConnectEx 비동기 접속, 수신 1개·송신 1개 상시 (송신은 큐에 모아 한 번에 WSASend)
//  - Dummy::lock 을 잡은 상태에서 Connect/Send/Close 호출
//  - 종료는 closesocket(abortive, TIME_WAIT 없음) → 진행 중 I/O가 모두 끝나면 Dummy::OnClosed
////////////////////////////////////////////////////////////////////////
#include "Common.h"
#include <vector>

class Dummy;

class Network
{
public:
	bool Start(int ioThreads, const std::string& ip, int port, std::string& err);
	void Stop();

	bool Connect(Dummy* d, uint32_t now);                   // lock 보유
	void Send(Dummy* d, const char* data, int len);         // lock 보유
	// reason: 로그용 원인 (정적 문자열), err: Windows/WSA 오류 코드 (0 = 없음). 첫 원인만 기록
	void Close(Dummy* d, bool intended, uint32_t now, const char* reason, int err = 0);   // lock 보유

private:
	static unsigned __stdcall WorkerThread(void* arg);
	void Worker();
	void PostRecv(Dummy* d, uint32_t now);
	void PostSend(Dummy* d, uint32_t now);
	void ProcessRecv(Dummy* d, uint32_t now);
	void FinishClose(Dummy* d, uint32_t now);

	HANDLE m_iocp = nullptr;
	std::vector<HANDLE> m_threads;
	sockaddr_in m_addr{};
	LPFN_CONNECTEX m_connectEx = nullptr;
};

extern Network g_net;
