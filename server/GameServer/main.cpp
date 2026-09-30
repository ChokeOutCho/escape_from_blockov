#include "GameServer.h"
#include <string>
#ifndef GAME_STUB_NETLIB
#include "../ContentEchoServer/Utils/CrashDump.h"
#include "../ContentEchoServer/Utils/SystemMonitor.h"
#include <conio.h>
#endif

static float WaitMs(int ms, DWORD* prev)
{
	DWORD delta = timeGetTime() - *prev;
	*prev += delta;
	int t = ms - (int)delta;
	if (t > 0) { Sleep(t); delta += t; *prev += t; }
	return delta / 1000.0f;
}

// 콘솔 한 줄 출력 (이전 내용이 남지 않도록 줄 끝까지 공백으로 덮는다)
static void Line(const char* fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (n < 0) n = 0;
	printf("%s%*s\n", buf, n < 110 ? 110 - n : 0, "");
}

static void ConsoleHome()
{
#ifndef GAME_STUB_NETLIB
	COORD c{ 0, 0 };
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
#endif
}

int main()
{
	GameConfig cfg;
	if (!cfg.Load("game_config.txt"))
		printf("game_config.txt not found. using defaults\n");

	// 무기 사거리는 시야 보장 거리(섹터 크기 × 시야 반경) 이하
	float maxRange = MapConst::SectorSize * (float)cfg.viewSectorRadius;
	WeaponTable weapons;
	if (!weapons.Load(cfg.weaponsFile.c_str(), maxRange))
	{
		printf("%s not found or empty\n", cfg.weaponsFile.c_str());
		return -1;
	}
	if (weapons.Find((uint8_t)cfg.defaultWeaponId) == nullptr)
	{
		printf("default_weapon_id %d not in %s\n", cfg.defaultWeaponId, cfg.weaponsFile.c_str());
		return -1;
	}
	SpawnTable spawns;
	if (!spawns.Load(cfg.spawnsFile.c_str()))
		printf("%s not found. spawning at map center\n", cfg.spawnsFile.c_str());

	ObstacleMap obstacles;
	{
		std::string err;
		if (obstacles.LoadBmp(cfg.obstacleMapFile.c_str(), err))
			printf("obstacle map %s (%dx%d): wall %d, low %d cells, hash 0x%08X\n", cfg.obstacleMapFile.c_str(),
			       obstacles.ImageWidth(), obstacles.ImageHeight(), obstacles.WallCells(), obstacles.LowCells(), obstacles.Hash());
		else
			printf("[warn] obstacle map %s not loaded: %s → 엄폐물 없이 실행\n", cfg.obstacleMapFile.c_str(), err.c_str());
	}

	if (cfg.roomCount + 1 > cfg.workerThreads - 2)
		printf("[warn] room_count(%d)+1 > workerTH_Pool_size(%d)-2 : Content 바쁜 루프로 세션 I/O가 지연될 수 있음\n",
		       cfg.roomCount, cfg.workerThreads);

	GetServerTimeMs();  // 서버 시각 기준점 고정
	GameServer* server = new GameServer(cfg, weapons, spawns, obstacles);
	server->Start();
	printf("GameServer start port %d, rooms %d x %d, weapons %d, spawns %d\n",
	       cfg.port, cfg.roomCount, cfg.roomCapacity, (int)weapons.All().size(), (int)spawns.All().size());
	if (cfg.testMode)
		printf("[TEST MODE] 모든 플레이어를 섹터 (%d, %d)에 스폰\n", cfg.testSpawnSectorX, cfg.testSpawnSectorY);

#ifndef GAME_STUB_NETLIB
	SystemMonitor sys;
	Sleep(1000);
	system("cls");
	// 커서 숨김
	CONSOLE_CURSOR_INFO ci{ 1, FALSE };
	SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
	// 빠른 편집 모드 끄기: 콘솔을 클릭해 선택 상태가 되면 출력이 멈추는 문제 방지
	{
		HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
		DWORD mode = 0;
		if (GetConsoleMode(hin, &mode))
			SetConsoleMode(hin, (mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
	}
#endif

	DWORD prev = timeGetTime();
	float acc = 0;
	bool running = true;
	while (running)
	{
		acc += WaitMs(200, &prev);
#ifndef GAME_STUB_NETLIB
		if (_kbhit())
		{
			int key = _getch();
			if (key == 'q' || key == 'Q') running = false;
		}
#endif
		if (acc < 1.0f) continue;
		acc = 0;

		uint32_t up = GetServerTimeMs() / 1000;
		ConsoleHome();
		Line("==================== escape_from_blockov GameServer ====================");
		Line(" Uptime %02u:%02u:%02u   port %d   protocol v%u   [Q] quit", up / 3600, (up / 60) % 60, up % 60,
		     cfg.port, GAME_PROTOCOL_VERSION);
		if (cfg.testMode)
			Line(" [TEST MODE] 모든 플레이어 스폰 = 섹터 (%d, %d)   (game_config.txt test_mode)", cfg.testSpawnSectorX, cfg.testSpawnSectorY);
		else
			Line("");
		Line(" [Sessions]  total %d   entry(waiting) %d", server->GetSessionCount(), server->Entry()->WaitingCount());
		{
			std::string rooms;
			char tmp[48];
			for (BattleContent* r : server->Rooms())
			{
				snprintf(tmp, sizeof(tmp), " #%d %d/%d", r->RoomNo(), r->PlayerCount(), cfg.roomCapacity);
				rooms += tmp;
			}
			Line(" [Rooms]    %s", rooms.c_str());
		}
		Line("");
		Line(" [Network]   Recv %9.1f KB/s  %7d pkt/s (TPS)", server->GetBPS_Recv() / 1024.0, server->GetTPS_Recv());
		Line("             Send %9.1f KB/s  %7d pkt/s (TPS)", server->GetBPS_Send() / 1024.0, server->GetTPS_Send());
		Line("             Accept %d /s", server->GetTPS_Accept());
		Line(" [Pool]      Packet pool %d (생성된 패킷 수)", server->GetPacketUseSize());
#ifndef GAME_STUB_NETLIB
		sys.UpdateCpuTime();
		Line("");
		Line(" [CPU]       System %5.1f%% (user %5.1f / kernel %5.1f)", sys.ProcessorTotal(), sys.ProcessorUser(), sys.ProcessorKernel());
		Line("             Process %5.1f%% (user %5.1f / kernel %5.1f)", sys.ProcessTotal(), sys.ProcessUser(), sys.ProcessKernel());
		Line(" [Memory]    Available %6ld MB   NonPaged pool %5lld MB   Process private %5lld MB",
		     sys.GetAvailMemMB(), sys.GetNonPagedMB(), sys.GetProcessPrivateMB());
		Line(" [NIC]       Send %8lld KB/s   Recv %8lld KB/s (전체 네트워크 어댑터, 루프백 제외)",
		     sys.GetTotalSendKBps(), sys.GetTotalRecvKBps());
#endif
		Line("");
		Line(" [Map]       %s: %s  wall %d  low %d  hash 0x%08X", cfg.obstacleMapFile.c_str(),
		     obstacles.Loaded() ? "loaded" : "NOT LOADED", obstacles.WallCells(), obstacles.LowCells(), obstacles.Hash());
		Line("------------------------------- log -----------------------------------");
		{
			auto logs = GameLogBuffer::Instance().Tail(10);
			for (size_t i = 0; i < 10; i++)
				Line(" %s", i < logs.size() ? logs[i].c_str() : "");
		}
		Line("========================================================================");
		fflush(stdout);
	}
	// NetLib_Server::Stop()은 종료 시 크래시 이슈가 있어 호출하지 않는다 (라이브러리 문서 8장)
	return 0;
}
