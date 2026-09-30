#include "NetLib/NetLib_Server.h"
#include "CommonProtocol.h"
#include "ContentServer.h"
#include "Utils/CrashDump.h"
#include "MonitorClient.h"
#include "Utils/SystemMonitor.h"
#include "Utils/Parser.h"
#include <chrono>

ContentServer* server;
MonitorClient* monitorClient;
SystemMonitor system_monitor;
bool controlMode;
bool control_monitor = true;
long isRunning = true;
float WaitForTime(int tick, DWORD* prevTime);
void ServerControl();
void Monitoring(float deltaTime);

int main()
{
	DWORD prevTime = timeGetTime();


	int opt_serverport;
	int opt_poolsize;
	int opt_concurrentsize;
	int opt_maxofsession;
	bool opt_encryption;
	bool opt_zerocpy;
	int opt_encryption_header_code;
	int opt_encryption_fixed_key;
	int opt_sendbuf;
	{
		Parser parser;
		if (parser.loadFromFile("echo_config.txt"))
		{
			opt_serverport = parser.GetInt("port");
			opt_poolsize = parser.GetInt("workerTH_Pool_size");
			opt_concurrentsize = parser.GetInt("concurrentTH_size");
			opt_maxofsession = parser.GetInt("maxofsession");
			opt_encryption = parser.GetBool("encryption");

			opt_encryption_header_code = (char)parser.GetInt("encryption_header_code");
			opt_encryption_fixed_key = (char)parser.GetInt("encryption_fixed_key");

			opt_zerocpy = parser.GetBool("zerocopy");

			opt_sendbuf = parser.GetInt("sendbuf");
		}
		else
		{
			printf("config파일이 없습니다!!!");
			wint_t key = _getwch();
			return -1;
		}
	}
	Opt_Encryption encryption = { opt_encryption_header_code, opt_encryption_fixed_key };

	// encryption=false면 nullptr 전달 → 인코딩/체크섬 미사용 (헤더 Code 0x77만 검사)
	server = new ContentServer(L"0.0.0.0", opt_serverport, opt_poolsize, opt_concurrentsize, 
								opt_maxofsession, opt_zerocpy, opt_encryption ? &encryption : nullptr, opt_sendbuf);

	server->Start();
	printf("Server Start\n");
	printf("Wait monitor server..");

	Opt_Encryption opt_monitor_encrypt = { 109 , 30 };
	monitorClient = new MonitorClient(SERVER_NO, L"127.0.0.1", 21107, 2, 2, true, false, &opt_monitor_encrypt);
	while (1)
	{
		if (monitorClient->Connect() == true)
		{
			printf("\nMonitor Server has connect!\n");
			break;
		}

		printf(".");
	}

	while (isRunning)
	{
		float deltaTime = WaitForTime(500, &prevTime);
		Monitoring(deltaTime);
		ServerControl();


	} // end while
	return 0;
}

float WaitForTime(int tick, DWORD* prevTime)
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

float delta_cumulative;
float runningtime = 0;
void Monitoring(float deltaTime)
{
	delta_cumulative += deltaTime;

	if (delta_cumulative >= 1)
	{

		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		int timestamp = static_cast<int>(t);

		system_monitor.UpdateCpuTime();
		runningtime += delta_cumulative;
		delta_cumulative = 0;
		long availMem = system_monitor.GetAvailMemMB();
		long long processMem = system_monitor.GetProcessPrivateMB();
		long npMem = system_monitor.GetNonPagedMB();
		long netSend = (long)system_monitor.GetTotalSendKBps();
		long netRecv = (long)system_monitor.GetTotalRecvKBps();
		float processorTotal = system_monitor.ProcessorTotal();
		float processorUser = system_monitor.ProcessorKernel();
		float processorKernel = system_monitor.ProcessorKernel();
		float processTotal = system_monitor.ProcessTotal();
		float processUser = system_monitor.ProcessUser();
		float processKernel = system_monitor.ProcessKernel();

		long sessionCount = server->GetSessionCount();
		long packetPoolSize = server->GetPacketUseSize();
		
		long tps_recv = server->GetTPS_Recv();
		long tps_send = server->GetTPS_Send();

		long authPlayerCount = AuthContent::sAuthSessionCount;
		long gamePlayerCount = EchoContent::sEchoSessionCount;

		long acceptTPS = server->GetTPS_Accept();
		long dbWriteTPS = 0;
		long dbWriteMSG = 0;
		long authThreadFPS = server->GetAuthFPS();
		long gameThreadFPS = server->GetEchoFPS();

		if (monitorClient->IsConnect() == true)
		{
			Packet* monitorpacket_run = Packet::Alloc();
			*monitorpacket_run << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_RUN << 1 << timestamp;
			monitorClient->SendPacket(monitorpacket_run);
			Packet::Free(monitorpacket_run);

			Packet* monitorpacket_cpu = Packet::Alloc();
			Packet* monitorpacket_mem = Packet::Alloc();
			Packet* monitorpacket_session = Packet::Alloc();
			Packet* monitorpacket_authPlayer = Packet::Alloc();
			Packet* monitorpacket_gamePlayer = Packet::Alloc();
			Packet* monitorpacket_acceptTPS = Packet::Alloc();
			Packet* monitorpacket_packetRecv = Packet::Alloc();
			Packet* monitorpacket_packetSend = Packet::Alloc();
			Packet* monitorpacket_dbWriteTPS = Packet::Alloc();
			Packet* monitorpacket_dbWriteMSG = Packet::Alloc();
			Packet* monitorpacket_authFPS = Packet::Alloc();
			Packet* monitorpacket_gameFPS = Packet::Alloc();
			Packet* monitorpacket_packetPool = Packet::Alloc();

			*monitorpacket_cpu << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_CPU << (int)processTotal << timestamp;
			*monitorpacket_mem << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SERVER_MEM << (int)processMem << timestamp;
			*monitorpacket_session << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_SESSION << (int)sessionCount << timestamp;
			*monitorpacket_authPlayer << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_AUTH_PLAYER << (int)authPlayerCount << timestamp;
			*monitorpacket_gamePlayer << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_GAME_PLAYER << (int)gamePlayerCount << timestamp;
			*monitorpacket_acceptTPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_ACCEPT_TPS << (int)acceptTPS << timestamp;
			*monitorpacket_packetRecv << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_PACKET_RECV_TPS << (int)tps_recv << timestamp;
			*monitorpacket_packetSend << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_PACKET_SEND_TPS << (int)tps_send << timestamp;
			*monitorpacket_dbWriteTPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_DB_WRITE_TPS << (int)dbWriteTPS << timestamp;
			*monitorpacket_dbWriteMSG << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_DB_WRITE_MSG << (int)dbWriteMSG << timestamp;
			*monitorpacket_authFPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_AUTH_THREAD_FPS << (int)authThreadFPS << timestamp;
			*monitorpacket_gameFPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_GAME_THREAD_FPS << (int)gameThreadFPS << timestamp;
			*monitorpacket_packetPool << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_GAME_PACKET_POOL << (int)packetPoolSize << timestamp;

			monitorClient->SendPacket(monitorpacket_cpu);
			monitorClient->SendPacket(monitorpacket_mem);
			monitorClient->SendPacket(monitorpacket_session);
			monitorClient->SendPacket(monitorpacket_authPlayer);
			monitorClient->SendPacket(monitorpacket_gamePlayer);
			monitorClient->SendPacket(monitorpacket_acceptTPS);
			monitorClient->SendPacket(monitorpacket_packetRecv);
			monitorClient->SendPacket(monitorpacket_packetSend);
			monitorClient->SendPacket(monitorpacket_dbWriteTPS);
			monitorClient->SendPacket(monitorpacket_dbWriteMSG);
			monitorClient->SendPacket(monitorpacket_authFPS);
			monitorClient->SendPacket(monitorpacket_gameFPS);
			monitorClient->SendPacket(monitorpacket_packetPool);

			Packet::Free(monitorpacket_cpu);
			Packet::Free(monitorpacket_mem);
			Packet::Free(monitorpacket_session);
			Packet::Free(monitorpacket_authPlayer);
			Packet::Free(monitorpacket_gamePlayer);
			Packet::Free(monitorpacket_acceptTPS);
			Packet::Free(monitorpacket_packetRecv);
			Packet::Free(monitorpacket_packetSend);
			Packet::Free(monitorpacket_dbWriteTPS);
			Packet::Free(monitorpacket_dbWriteMSG);
			Packet::Free(monitorpacket_authFPS);
			Packet::Free(monitorpacket_gameFPS);
			Packet::Free(monitorpacket_packetPool);
		}
		if (control_monitor)
		{
			printf("\n-------------------------------------------------------------------------");
			printf("\n\n**   ECHO SERVER   **\n\n");
			printf("Running Time:  %20.2f  sec\n", runningtime);
			printf("\nMonitor Server Connect: ");

			if (monitorClient->IsConnect() == true) printf("TRUE");
			else printf("FALSE");

			printf("\n\nSession Count:              %7d", sessionCount);
			printf("\nPacket Pool Use:            %7d", packetPoolSize);
			printf("\nTotal_Accept:               %7d", server->GetTotal_Accept());
			printf("\nTPS_Accept:                 %7d", server->GetTPS_Accept());
			printf("\nTPS_Send:                   %7d", tps_send);
			printf("\nPlayerPool:                 %7d", Player::PlayerPoolCurrentSize());
			printf("\nTPS_Recv:                   %7d", tps_recv);
			printf("\nAuth Session Count:		  %7d", authPlayerCount);
			printf("\nEcho Session Count:		  %7d", gamePlayerCount);
			printf("\nWaitSession Pool:           %7d", WaitingSession::GetPoolUseSize());
			//printf("\nWaiting Queue:              %7lld", waiting.size());

			//printf("\n\nPlayer Count Auth/Session:    %7lld       /%7ld", sessions_accNo.size(), playerCount);
			//printf("\nPlayer Pool Use:              %7d", Player::PlayerPoolCurrentSize());
			//printf("\n\nDuplicate login:              %7d", duplicateLogin);

			printf("\n\n\n**   Hardware   **\n");
			printf("\nProcessorTotal: %6.2f%%      ProcessorUser: %6.2f%%     ProcessorKernel: %6.2f%%", processorTotal, processorUser, processorKernel);
			printf("\nProcessTotal:   %6.2f%%      ProcessUser: %6.2f%%       ProcessKernel: %6.2f%%", processTotal, processorUser, processKernel);
			printf("\nAvaliable Mem:  %8d MB       Process Mem:  %8lld MB     NonPagedMem: %8d MB", availMem, processMem, npMem);
			printf("\nNet Send: %8d KB/s         Net Recv: %8d KB/s", netSend, netRecv);
			printf("\n\nauth fps: %d   echo fps: %d", authThreadFPS, gameThreadFPS);
			printf("\nPacket Recv TPS: %8d          Packet Send TPS: %8d", tps_recv, tps_send);
			printf("\n-------------------------------------------------------------------------\n");
		}
	}

}

void ServerControl()
{
	if (_kbhit())
	{
		wint_t key = _getwch();
		if (key == 'u' || key == 'U')
		{
			controlMode = true;
			printf("Control Mode: Press Q - Quit \n");
			printf("Control Mode: Press L - Key Lock \n");
			printf("Control Mode: Press F - Monitor \n");
			printf("Control Mode: Press P - PROFILE PRINT\n");
			printf("Control Mode: Press R - PROFILE RESET\n");
			printf("Control Mode: Press C - CRASH\n");
			printf("Control Mode: Press B - CLEAR BLACKLIST\n");

		}

		if (controlMode)
		{
			switch (key)
			{
			case 's':
				[[fallthrough]];
			case 'S':
				//printf("accept스탑");
				//server->AcceptPause();
				break;
			case 'l':
				[[fallthrough]];
			case 'L':
				controlMode = false;
				printf("Control Lock: Press U - Control Unlock \n");
				break;
			case'f':
				[[fallthrough]];
			case'F':
				printf("\nControl Monitor\n \n");

				control_monitor = !control_monitor;
				break;
			case'q':
				[[fallthrough]];
			case'Q':
				server->Stop();
				isRunning = false;
				break;
			case'p':
				[[fallthrough]];
			case'P':
				Profiler::FlushToFile();
				printf("프로파일러 쓰기 완료\n");
				break;
			case'r':
				[[fallthrough]];
			case'R':
				Profiler::Reset();
				printf("프로파일러 리셋 완료\n");
				break;
			case 'w':
				[[fallthrough]];
			case 'W':
				server->DisconnectAll();
				break;
			case 'c':
				[[fallthrough]];
			case'C':
				CrashDump::Crash();
				break;
			case 'b':
				[[fallthrough]];
			case'B':
				server->ClearBlacklist();
				break;
			default:
				break;

			}
		}

	}
}