#pragma once
#include "Define.h"
#include "NetLib/NetLib_Client.h"
#include "CommonProtocol.h"
#include <chrono>
#include "conio.h"
class MonitorClient : public NetLib_Client
{
public:
	MonitorClient(int ServerNo, const WCHAR* connectIP, unsigned short connectPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, bool opt_zerocpy, bool opt_use_monitor, Opt_Encryption* opt_encryption)
		:NetLib_Client(connectIP, connectPort, opt_workerTH_Pool_size, opt_concurrentTH_size, opt_zerocpy, opt_use_monitor, opt_encryption)
	{
		m_serverNo = ServerNo;
	}
	virtual void OnEnterJoinServer()
	{
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		int timestamp = static_cast<int>(t);

		Packet* login = Packet::Alloc();
		*login << (WORD)en_PACKET_SS_MONITOR_LOGIN << m_serverNo;
		SendPacket(login);
		Packet::Free(login);
	}
	virtual void OnLeaveServer()
	{
		while (1)
		{
			if (Connect() == true)
				break;

			Sleep(3000);
		}
	}
	virtual void OnRecv(Packet* packet)
	{
		// 받는다고 할건 없다.
	}
private:
	int m_serverNo;
};