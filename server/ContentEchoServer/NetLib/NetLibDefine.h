#pragma once
#include <winsock2.h>

// 세션 소켓 수신 링버퍼 크기 (실사용 용량 = size - 1)
#define SESSION_RECV_BUFFER_SIZE 2048
// 세션 컨텐츠 수신 링버퍼 크기 (ContentQueueHeader + payload 누적, Content 틱마다 소비)
#define SESSION_CONTENT_BUFFER_SIZE 2048
struct MSG_CONTENT
{
	unsigned long long SessionHandle;
	void* Packet;
};

struct TPS_SET
{
	int send;
	int recv;
};

enum OVERLAPPED_TYPE
{
	SESSION = 0,
	CONTENT,
};

struct CUSTOM_OVERLAPPED
{
	WSAOVERLAPPED overlapped;
	OVERLAPPED_TYPE type;
};