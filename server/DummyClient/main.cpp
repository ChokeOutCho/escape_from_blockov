////////////////////////////////////////////////////////////////////////
// escape_from_blockov 더미 클라이언트 (스트레스 / 플레이 테스트)
//
//  대화형:  DummyClient.exe                 → 인원 수 입력 후 대시보드
//  무인:    DummyClient.exe --count 1000 --duration 60 [--server 127.0.0.1:10301]
//           [--config dummy_config.txt] [--leave] [--nofire]
//
//  키: [C] 인원 변경  [+]/[-] 100명 증감  [M] 사망 모드  [F] 사격  [Q] 종료
////////////////////////////////////////////////////////////////////////
#include "Common.h"
#include "Dummy.h"
#include "Network.h"
#include "EventLog.h"
#include "../ContentEchoServer/Utils/CrashDump.h"
#include <process.h>
#include <conio.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <algorithm>
#include <random>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

Stats g_stats;
DummyConfig g_cfg;
ObstacleMap g_map;
std::atomic<int> g_targetCount{ 0 };
std::atomic<bool> g_fireEnabled{ true };
std::atomic<bool> g_reconnectMode{ true };
std::atomic<int> g_connectTokens{ 0 };
std::atomic<uint32_t> g_serverMapHash{ 0 };
std::vector<std::u16string> g_names;

namespace
{
	std::vector<Dummy*> g_dummies;          // 생성만 하고 해제하지 않는다
	std::atomic<int> g_allocated{ 0 };      // 로직 스레드가 순회할 개수
	std::atomic<bool> g_quit{ false };
	std::vector<HANDLE> g_logicThreads;

	// 목표 인원까지 Dummy 객체를 미리 만든다 (메인 스레드에서만 호출)
	void EnsureAllocated(int n)
	{
		n = std::min(n, g_cfg.maxDummies);
		int cur = g_allocated.load();
		for (int i = cur; i < n; i++)
		{
			Dummy* d = new Dummy();
			d->Init(i);
			g_dummies[i] = d;
		}
		if (n > cur) g_allocated.store(n, std::memory_order_release);
	}

	void SetTargetCount(int n)
	{
		if (n < 0) n = 0;
		if (n > g_cfg.maxDummies) n = g_cfg.maxDummies;
		EnsureAllocated(n);
		g_targetCount.store(n);
	}

	unsigned __stdcall LogicThread(void* arg)
	{
		int t = (int)(intptr_t)arg;
		int L = g_cfg.logicThreads;
		uint32_t next = NowMs();
		while (!g_quit.load())
		{
			uint32_t now = NowMs();
			int n = g_allocated.load(std::memory_order_acquire);
			for (int i = t; i < n; i += L)
			{
				Dummy* d = g_dummies[i];
				AcquireSRWLockExclusive(&d->lock);
				d->Tick(now);
				ReleaseSRWLockExclusive(&d->lock);
			}
			// 첫 번째 로직 스레드가 접속 토큰 충전 (틱마다 connect_per_sec * tick 만큼, 최대 0.2초치)
			if (t == 0)
			{
				int add = std::max(1, g_cfg.connectPerSec * g_cfg.tickMs / 1000);
				int cap = std::max(1, g_cfg.connectPerSec / 5);
				int v = g_connectTokens.load();
				while (!g_connectTokens.compare_exchange_weak(v, std::min(cap, v + add))) {}
			}
			next += (uint32_t)g_cfg.tickMs;
			int wait = TimeDiff(next, NowMs());
			if (wait > 0) Sleep((DWORD)wait);
			else next = NowMs();    // 밀렸으면 따라잡지 않는다
		}
		return 0;
	}

	////////////////////////////////////////////////////////////////////
	// 통계 스냅샷 / 대시보드
	////////////////////////////////////////////////////////////////////
	struct Snapshot
	{
		long long sendBytes, recvBytes, sendPkts, recvPkts, shots, hitsReported, hitsConfirmed, deaths, kills, corrections, rolls;
		long long connectOk, connectFail, disconnects, unexpected, enterOk, enterFull, enterOther, enterTimeout;
		long long kicks[5];
	};

	Snapshot Take()
	{
		Snapshot s;
		s.sendBytes = g_stats.sendBytes; s.recvBytes = g_stats.recvBytes; s.sendPkts = g_stats.sendPkts; s.recvPkts = g_stats.recvPkts;
		s.shots = g_stats.shots; s.hitsReported = g_stats.hitsReported; s.hitsConfirmed = g_stats.hitsConfirmed;
		s.deaths = g_stats.deaths; s.kills = g_stats.kills; s.corrections = g_stats.corrections; s.rolls = g_stats.rolls;
		s.connectOk = g_stats.connectOk; s.connectFail = g_stats.connectFail; s.disconnects = g_stats.disconnects;
		s.unexpected = g_stats.unexpectedDisconnects;
		s.enterOk = g_stats.enterOk; s.enterFull = g_stats.enterFull; s.enterOther = g_stats.enterOther; s.enterTimeout = g_stats.enterTimeout;
		for (int i = 0; i < 5; i++) s.kicks[i] = g_stats.kicks[i];
		return s;
	}

	struct StateCounts { int idle = 0, connecting = 0, entering = 0, inGame = 0, dead = 0, closing = 0; double weakSum = 0; };

	StateCounts CountStates()
	{
		StateCounts c;
		int n = g_allocated.load();
		for (int i = 0; i < n; i++)
		{
			const Dummy* d = g_dummies[i];
			c.weakSum += d->Weakness();
			NetState ns = d->Net();
			if (ns == NetState::Idle) c.idle++;
			else if (ns == NetState::Connecting) c.connecting++;
			else if (ns == NetState::Closing) c.closing++;
			else
			{
				GameState gs = d->Game();
				if (gs == GameState::InGame) c.inGame++;
				else if (gs == GameState::Dead) c.dead++;
				else c.entering++;
			}
		}
		return c;
	}

	// 프로세스 CPU 사용률 (전체 코어 기준 %)
	double ProcessCpu()
	{
		static ULONGLONG lastSys = 0, lastProc = 0;
		FILETIME ftNow, c, e, k, u;
		GetSystemTimeAsFileTime(&ftNow);
		GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
		ULONGLONG now = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;
		ULONGLONG proc = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
		double pct = 0;
		if (lastSys)
		{
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			double dt = (double)(now - lastSys);
			if (dt > 0) pct = (double)(proc - lastProc) / dt / si.dwNumberOfProcessors * 100.0;
		}
		lastSys = now;
		lastProc = proc;
		return pct;
	}

	double ProcessMemMB()
	{
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
		return pmc.PrivateUsage / (1024.0 * 1024.0);
	}

	void ConsoleHome()
	{
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		COORD pos{ 0, 0 };
		SetConsoleCursorPosition(h, pos);
	}

	void Line(const char* fmt, ...)
	{
		char buf[512];
		va_list args;
		va_start(args, fmt);
		int n = vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		printf("%s%*s\n", buf, n < 110 ? 110 - n : 0, "");
	}

	void DrawDashboard(const Snapshot& cur, const Snapshot& prev, double sec, uint32_t uptimeSec, double cpu, double mem)
	{
		StateCounts st = CountStates();
		long long rc = g_stats.rttCount.exchange(0);
		long long rs = g_stats.rttSum.exchange(0);
		int rmax = g_stats.rttMax.exchange(0);
		auto rate = [&](long long a, long long b) { return (double)(a - b) / sec; };

		ConsoleHome();
		Line("==================== escape_from_blockov DummyClient ====================");
		Line(" Server %s:%d   Uptime %02u:%02u:%02u   protocol v%u", g_cfg.serverIp.c_str(), g_cfg.serverPort,
		     uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60, GAME_PROTOCOL_VERSION);
		Line(" [C] 인원 변경  [+]/[-] 100명  [M] 사망 모드  [F] 사격  [Q] 종료");
		Line("");
		Line(" [설정]    목표 %d명 (생성 %d)   사망 시: %s   사격: %s   맵: %s",
		     g_targetCount.load(), g_allocated.load(),
		     g_reconnectMode.load() ? "재접속" : "퇴장", g_fireEnabled.load() ? "ON" : "OFF",
		     g_map.Loaded() ? "로드됨" : "없음(엄폐물 무시)");
		{
			uint32_t sh = g_serverMapHash.load();
			if (sh != 0 && (!g_map.Loaded() || sh != g_map.Hash()))
				Line(" [경고]    맵이 서버와 다름 (서버 0x%08X / 더미 0x%08X) → 이동이 거부되어 킥될 수 있음. obstacle_map 경로 확인", sh, g_map.Loaded() ? g_map.Hash() : 0u);
		}
		Line(" [상태]    게임중 %d   입장중 %d   사망 %d   접속중 %d   종료중 %d   대기 %d",
		     st.inGame, st.entering, st.dead, st.connecting, st.closing, st.idle);
		Line(" [접속]    성공 %lld (+%.0f/s)  실패 %lld   끊김 %lld (비정상 %lld)",
		     cur.connectOk, rate(cur.connectOk, prev.connectOk), cur.connectFail, cur.disconnects, cur.unexpected);
		Line(" [입장]    OK %lld   FULL %lld   거부 %lld   타임아웃 %lld",
		     cur.enterOk, cur.enterFull, cur.enterOther, cur.enterTimeout);
		Line("");
		Line(" [수신]    %9.1f KB/s  %8.0f pkt/s", rate(cur.recvBytes, prev.recvBytes) / 1024.0, rate(cur.recvPkts, prev.recvPkts));
		Line(" [송신]    %9.1f KB/s  %8.0f pkt/s", rate(cur.sendBytes, prev.sendBytes) / 1024.0, rate(cur.sendPkts, prev.sendPkts));
		Line(" [RTT]     평균 %lld ms   최대 %d ms   (최근 1초 표본 %lld)", rc ? rs / rc : 0, rmax, rc);
		Line("");
		Line(" [전투]    사격 %.0f/s   명중보고 %.0f/s   명중확인 %.0f/s   사망 %.0f/s   킬 %.0f/s   구르기 %.0f/s",
		     rate(cur.shots, prev.shots), rate(cur.hitsReported, prev.hitsReported), rate(cur.hitsConfirmed, prev.hitsConfirmed),
		     rate(cur.deaths, prev.deaths), rate(cur.kills, prev.kills), rate(cur.rolls, prev.rolls));
		{
			StateCounts wc = CountStates();
			int nAll = g_allocated.load();
			Line("           누적 사격 %lld  명중보고 %lld  명중확인 %lld  사망 %lld   약함 평균 %.2f (%.1f~%.1f)   특수 무기 획득 %lld",
			     cur.shots, cur.hitsReported, cur.hitsConfirmed, cur.deaths, nAll ? wc.weakSum / nAll : 0.0, g_cfg.weaknessMin, g_cfg.weaknessMax,
			     (long long)g_stats.specialPickups.load());
		}
		Line(" [검증]    위치보정 %.0f/s (누적 %lld)   킥: 타임아웃 %lld  잘못된패킷 %lld  치트의심 %lld  서버종료 %lld",
		     rate(cur.corrections, prev.corrections), cur.corrections, cur.kicks[1], cur.kicks[2], cur.kicks[3], cur.kicks[4]);
		Line("");
		Line(" [로그]    비정상 끊김 %lld   접속 실패 %lld   입장 실패 %lld   → %s",
		     g_eventLog.Count(EventLog::DISCONNECT), g_eventLog.Count(EventLog::CONNECT_FAIL), g_eventLog.Count(EventLog::ENTER_FAIL), g_eventLog.Path().c_str());
		Line(" [클라]    CPU %5.1f%%   메모리 %.0f MB   IO 스레드 %d   로직 스레드 %d (틱 %dms)",
		     cpu, mem, g_cfg.ioThreads, g_cfg.logicThreads, g_cfg.tickMs);
		Line("=========================================================================");
	}

	void PrintHeadlessLine(const Snapshot& cur, const Snapshot& prev, double sec, uint32_t uptimeSec, double cpu, double mem)
	{
		StateCounts st = CountStates();
		long long rc = g_stats.rttCount.exchange(0);
		long long rs = g_stats.rttSum.exchange(0);
		int rmax = g_stats.rttMax.exchange(0);
		auto rate = [&](long long a, long long b) { return (double)(a - b) / sec; };
		printf("[%4us] game %d enter %d dead %d conn %d idle %d | recv %.0fKB/s %.0fpkt/s send %.0fKB/s %.0fpkt/s | rtt avg %lld max %d | "
		       "shot %.0f/s hit %.0f/s confirm %.0f/s death %.0f/s roll %.0f/s | corr %lld kick %lld/%lld/%lld | full %lld fail %lld unexp %lld | log %lld/%lld/%lld | cpu %.1f%% mem %.0fMB\n",
		       uptimeSec, st.inGame, st.entering, st.dead, st.connecting, st.idle,
		       rate(cur.recvBytes, prev.recvBytes) / 1024.0, rate(cur.recvPkts, prev.recvPkts),
		       rate(cur.sendBytes, prev.sendBytes) / 1024.0, rate(cur.sendPkts, prev.sendPkts),
		       rc ? rs / rc : 0, rmax,
		       rate(cur.shots, prev.shots), rate(cur.hitsReported, prev.hitsReported), rate(cur.hitsConfirmed, prev.hitsConfirmed),
		       rate(cur.deaths, prev.deaths), rate(cur.rolls, prev.rolls),
		       cur.corrections, cur.kicks[1], cur.kicks[2], cur.kicks[3], cur.enterFull, cur.connectFail, cur.unexpected,
		       g_eventLog.Count(EventLog::DISCONNECT), g_eventLog.Count(EventLog::CONNECT_FAIL), g_eventLog.Count(EventLog::ENTER_FAIL), cpu, mem);
		fflush(stdout);
	}

	int ReadNumber(const char* prompt)
	{
		for (;;)
		{
			printf("%s", prompt);
			fflush(stdout);
			std::string line;
			if (!std::getline(std::cin, line)) return -1;
			try { return std::stoi(line); }
			catch (...) { printf("숫자를 입력하세요.\n"); }
		}
	}

	// 실행 파일을 x64\Release에서 직접 실행한 경우 등: 작업 폴더에 설정 파일이 없으면 실행 파일 위쪽 폴더에서 찾아 그곳으로 이동
	// (설정·엄폐물 맵·닉네임 목록·로그가 모두 server/DummyClient 기준 상대 경로이기 때문)
	void FixWorkingDirectory(const std::string& configPath)
	{
		if (GetFileAttributesA(configPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
		if (configPath.find(':') != std::string::npos || (!configPath.empty() && (configPath[0] == '\\' || configPath[0] == '/'))) return;
		char exe[MAX_PATH];
		DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return;
		std::string dir(exe, n);
		dir = dir.substr(0, dir.find_last_of("\\/"));
		for (const char* up : { "", "\\..", "\\..\\.." })
		{
			std::string d = dir + up;
			if (GetFileAttributesA((d + "\\" + configPath).c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				SetCurrentDirectoryA(d.c_str());
				char cwd[MAX_PATH];
				GetCurrentDirectoryA(MAX_PATH, cwd);
				printf("작업 폴더 -> %s (%s 위치)\n", cwd, configPath.c_str());
				return;
			}
		}
	}

	// 닉네임 목록 (UTF-8, 한 줄에 하나, # 주석). 12자(UTF-16) 넘는 이름은 자르고, 순서를 섞는다
	void LoadNames(const std::string& path)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) { printf("닉네임 목록 %s 없음 -> %s + 번호\n", path.c_str(), g_cfg.namePrefix.c_str()); return; }
		std::string all;
		char buf[4096];
		size_t r;
		while ((r = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, r);
		fclose(f);
		if (all.size() >= 3 && (unsigned char)all[0] == 0xEF && (unsigned char)all[1] == 0xBB && (unsigned char)all[2] == 0xBF) all.erase(0, 3);
		size_t pos = 0;
		while (pos <= all.size())
		{
			size_t e = all.find('\n', pos);
			if (e == std::string::npos) e = all.size();
			std::string line = all.substr(pos, e - pos);
			pos = e + 1;
			size_t hash = line.find('#');
			if (hash != std::string::npos) line = line.substr(0, hash);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
			while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line.erase(0, 1);
			if (line.empty()) continue;
			std::u16string name = Utf8ToU16(line);
			if (name.size() > (size_t)NAME_LEN) name.resize(NAME_LEN);
			g_names.push_back(name);
		}
		std::mt19937 rng((uint32_t)GetTickCount64());
		std::shuffle(g_names.begin(), g_names.end(), rng);
		printf("닉네임 목록 %s: %zu개\n", path.c_str(), g_names.size());
	}

	void DisableQuickEdit()
	{
		HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
		DWORD mode = 0;
		if (GetConsoleMode(h, &mode))
			SetConsoleMode(h, (mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
	}
}

// 크래시 직전 이벤트 로그에 한 줄 (game-spec 10.8). 덤프는 CrashDump가 dumps/에 쓴다
static void OnCrash(unsigned long code, const char* reason, const char* dumpPath)
{
	char msg[256];
	snprintf(msg, sizeof(msg), "%s exception 0x%08lX -> %s", reason, code, dumpPath);
	g_eventLog.WriteNoWait("CRASH", msg);
}

int main(int argc, char** argv)
{
	// ---- 인자 ----
	std::string configPath = "dummy_config.txt";
	int argCount = -1, duration = 0;
	std::string argServer;
	bool argLeave = false, argNoFire = false;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
		if (a == "--count") argCount = atoi(next().c_str());
		else if (a == "--duration") duration = atoi(next().c_str());
		else if (a == "--server") argServer = next();
		else if (a == "--config") configPath = next();
		else if (a == "--leave") argLeave = true;
		else if (a == "--nofire") argNoFire = true;
		else { printf("알 수 없는 인자: %s\n", a.c_str()); return 1; }
	}
	bool headless = duration > 0;

	FixWorkingDirectory(configPath);
	CrashDump::SetOnCrash(OnCrash);
	if (!g_cfg.Load(configPath.c_str()))
		printf("%s 없음 → 기본값 사용\n", configPath.c_str());
	if (!argServer.empty())
	{
		size_t c = argServer.rfind(':');
		if (c != std::string::npos) { g_cfg.serverIp = argServer.substr(0, c); g_cfg.serverPort = atoi(argServer.c_str() + c + 1); }
		else g_cfg.serverIp = argServer;
	}
	if (argLeave) g_cfg.reconnectOnDeath = false;
	if (argNoFire) g_cfg.fire = false;
	g_cfg.ioThreads = std::max(1, g_cfg.ioThreads);
	g_cfg.logicThreads = std::max(1, g_cfg.logicThreads);
	g_cfg.tickMs = std::max(10, g_cfg.tickMs);
	g_cfg.connectPerSec = std::max(1, g_cfg.connectPerSec);
	g_cfg.maxDummies = std::max(1, g_cfg.maxDummies);
	g_reconnectMode = g_cfg.reconnectOnDeath;
	g_fireEnabled = g_cfg.fire;

	timeBeginPeriod(1);

	LoadNames(g_cfg.namesFile);

	// ---- 엄폐물 맵 ----
	std::string err;
	if (g_map.LoadBmp(g_cfg.obstacleMap.c_str(), err))
		printf("obstacle map %s: wall %d, low %d, hash 0x%08X\n", g_cfg.obstacleMap.c_str(), g_map.WallCells(), g_map.LowCells(), g_map.Hash());
	else
		printf("[경고] 엄폐물 맵 %s 로드 실패 (%s) → 엄폐물 무시하고 이동. 서버가 이동을 거부해 더미가 킥될 수 있음\n", g_cfg.obstacleMap.c_str(), err.c_str());

	// ---- 인원 ----
	int count = argCount >= 0 ? argCount : g_cfg.count;
	if (count <= 0 && !headless)
	{
		char prompt[128];
		snprintf(prompt, sizeof(prompt), "접속할 더미 수 (최대 %d): ", g_cfg.maxDummies);
		count = ReadNumber(prompt);
		if (count < 0) return 0;
	}

	// ---- 비정상 이벤트 로그 ----
	{
		char header[256];
		snprintf(header, sizeof(header), "server %s:%d, count %d, death_mode %s, fire %s, protocol v%u",
		         g_cfg.serverIp.c_str(), g_cfg.serverPort, count, g_cfg.reconnectOnDeath ? "reconnect" : "leave", g_cfg.fire ? "on" : "off", GAME_PROTOCOL_VERSION);
		if (g_eventLog.Open(header)) printf("event log: %s\n", g_eventLog.Path().c_str());
		else printf("[경고] 로그 파일을 열 수 없음 (logs 폴더 권한 확인)\n");
	}

	// ---- 시작 ----
	if (!g_net.Start(g_cfg.ioThreads, g_cfg.serverIp, g_cfg.serverPort, err))
	{
		printf("네트워크 시작 실패: %s\n", err.c_str());
		return 1;
	}
	g_dummies.assign(g_cfg.maxDummies, nullptr);
	SetTargetCount(count);
	for (int t = 0; t < g_cfg.logicThreads; t++)
		g_logicThreads.push_back((HANDLE)_beginthreadex(nullptr, 0, LogicThread, (void*)(intptr_t)t, 0, nullptr));

	printf("DummyClient start: %s:%d, %d dummies, io %d, logic %d, %d conn/s\n",
	       g_cfg.serverIp.c_str(), g_cfg.serverPort, count, g_cfg.ioThreads, g_cfg.logicThreads, g_cfg.connectPerSec);

	uint32_t start = NowMs();
	uint32_t lastDraw = start;
	Snapshot prev = Take();
	ProcessCpu();
	if (!headless)
	{
		DisableQuickEdit();
		system("cls");
	}

	// ---- 메인 루프: 대시보드 / 키 입력 ----
	for (;;)
	{
		Sleep(50);
		uint32_t now = NowMs();
		uint32_t up = (uint32_t)TimeDiff(now, start) / 1000;

		if (headless)
		{
			if (TimeDiff(now, lastDraw) >= 5000)
			{
				g_eventLog.Flush();
				Snapshot cur = Take();
				PrintHeadlessLine(cur, prev, TimeDiff(now, lastDraw) / 1000.0, up, ProcessCpu(), ProcessMemMB());
				prev = cur;
				lastDraw = now;
			}
			if ((int)up >= duration) break;
			continue;
		}

		if (_kbhit())
		{
			int ch = _getch();
			if (ch == 'q' || ch == 'Q') break;
			else if (ch == 'm' || ch == 'M') g_reconnectMode = !g_reconnectMode.load();
			else if (ch == 'f' || ch == 'F') g_fireEnabled = !g_fireEnabled.load();
			else if (ch == '+') SetTargetCount(g_targetCount.load() + 100);
			else if (ch == '-') SetTargetCount(g_targetCount.load() - 100);
			else if (ch == 'c' || ch == 'C')
			{
				system("cls");
				char prompt[128];
				snprintf(prompt, sizeof(prompt), "새 인원 수 (현재 %d, 최대 %d): ", g_targetCount.load(), g_cfg.maxDummies);
				int n = ReadNumber(prompt);
				if (n >= 0)
				{
					// 퇴장 모드에서 빠진 더미들도 다시 채운다
					int alloc = g_allocated.load();
					for (int i = 0; i < alloc; i++)
					{
						AcquireSRWLockExclusive(&g_dummies[i]->lock);
						g_dummies[i]->ClearRetired();
						ReleaseSRWLockExclusive(&g_dummies[i]->lock);
					}
					SetTargetCount(n);
				}
				system("cls");
			}
		}

		if (TimeDiff(now, lastDraw) >= 1000)
		{
			g_eventLog.Flush();
			Snapshot cur = Take();
			DrawDashboard(cur, prev, TimeDiff(now, lastDraw) / 1000.0, up, ProcessCpu(), ProcessMemMB());
			prev = cur;
			lastDraw = now;
		}
	}

	// ---- 종료: 전원 퇴장 후 스레드 정리 ----
	printf("\n종료 중... (연결 정리)\n");
	g_targetCount = 0;
	uint32_t waitStart = NowMs();
	while (TimeDiff(NowMs(), waitStart) < 5000)
	{
		StateCounts st = CountStates();
		if (st.idle == g_allocated.load()) break;
		Sleep(100);
	}
	g_quit = true;
	if (!g_logicThreads.empty())
		WaitForMultipleObjects((DWORD)g_logicThreads.size(), g_logicThreads.data(), TRUE, 5000);
	g_net.Stop();
	timeEndPeriod(1);
	g_eventLog.Close();

	Snapshot fin = Take();
	printf("summary: connect %lld (fail %lld), enter ok %lld full %lld other %lld timeout %lld, disconnects %lld (unexpected %lld)\n",
	       fin.connectOk, fin.connectFail, fin.enterOk, fin.enterFull, fin.enterOther, fin.enterTimeout, fin.disconnects, fin.unexpected);
	printf("         shots %lld, hits reported %lld, confirmed %lld, deaths %lld, kills %lld, rolls %lld, special pickups %lld, corrections %lld, kicks %lld/%lld/%lld/%lld\n",
	       fin.shots, fin.hitsReported, fin.hitsConfirmed, fin.deaths, fin.kills, fin.rolls, (long long)g_stats.specialPickups.load(), fin.corrections,
	       fin.kicks[1], fin.kicks[2], fin.kicks[3], fin.kicks[4]);
	printf("         abnormal log: disconnect %lld, connect fail %lld, enter fail %lld -> %s\n",
	       g_eventLog.Count(EventLog::DISCONNECT), g_eventLog.Count(EventLog::CONNECT_FAIL), g_eventLog.Count(EventLog::ENTER_FAIL), g_eventLog.Path().c_str());
	return 0;
}
