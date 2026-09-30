#pragma once
#include <windows.h>
#include <pdh.h>
#include <stdexcept>
#include <psapi.h>
#include <iphlpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "pdh.lib")

class SystemMonitor
{
private:
	PDH_HQUERY hQuery;
	PDH_HCOUNTER hCounterCPU, hCounterMem, hCounterNP, hCounterSend, hCounterRecv;
	HANDLE m_process;
	int m_numberOfProcessors;
	float m_processorTotal;
	float m_processorUser;
	float m_processorKernel;
	float m_processTotal;
	float m_processUser;
	float m_processKernel;
	ULARGE_INTEGER m_processor_lastKernel;
	ULARGE_INTEGER m_processor_lastUser;
	ULARGE_INTEGER m_processor_lastIdle;
	ULARGE_INTEGER m_process_lastKernel;
	ULARGE_INTEGER m_process_lastUser;
	ULARGE_INTEGER m_process_lastTime;
public:
	SystemMonitor()
	{
		if (PdhOpenQuery(NULL, 0, &hQuery) != ERROR_SUCCESS)
			throw std::runtime_error("Failed to open PDH query");

		PdhAddCounter(hQuery, L"\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);
		PdhAddCounter(hQuery, L"\\Memory\\Available MBytes", 0, &hCounterMem);
		PdhAddCounter(hQuery, L"\\Memory\\Pool Nonpaged Bytes", 0, &hCounterNP);
		PdhAddCounter(hQuery, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &hCounterSend);
		PdhAddCounter(hQuery, L"\\Network Interface(*)\\Bytes Received/sec", 0, &hCounterRecv);

		PdhCollectQueryData(hQuery);

		SYSTEM_INFO SystemInfo;
		GetSystemInfo(&SystemInfo);
		m_numberOfProcessors = SystemInfo.dwNumberOfProcessors;

		m_process = GetCurrentProcess();
		m_processorTotal = 0;
		m_processorUser = 0;
		m_processorKernel = 0;
		m_processTotal = 0;
		m_processUser = 0;
		m_processKernel = 0;
		m_processor_lastKernel.QuadPart = 0;
		m_processor_lastUser.QuadPart = 0;
		m_processor_lastIdle.QuadPart = 0;
		m_process_lastUser.QuadPart = 0;
		m_process_lastKernel.QuadPart = 0;
		m_process_lastTime.QuadPart = 0;
		UpdateCpuTime();
	}


	long long GetProcessPrivateMB()
	{
		PROCESS_MEMORY_COUNTERS_EX pmc;
		if (GetProcessMemoryInfo(m_process, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
		{
			return pmc.PrivateUsage / 1024 / 1024; // MB 단위
		}
		return -1;
	}

	long long GetProcessWorkingSetMB()
	{
		PROCESS_MEMORY_COUNTERS_EX pmc;
		if (GetProcessMemoryInfo(m_process, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
		{
			return pmc.WorkingSetSize / 1024 / 1024; // MB 단위
		}
		return -1;
	}

	long GetAvailMemMB() { return (long)GetCounterValue(hCounterMem, PDH_FMT_LONG); }
	long long GetNonPagedMB() { return (long long)(GetCounterValue(hCounterNP, PDH_FMT_LARGE) / 1024 / 1024); }

	double GetTcpSendKBps() { return GetCounterValue(hCounterSend, PDH_FMT_DOUBLE) / 1024.0; }
	double GetTcpRecvKBps() { return GetCounterValue(hCounterRecv, PDH_FMT_DOUBLE) / 1024.0; }

	long long GetTotalSendKBps()
	{
		unsigned long long inBytes, outBytes;
		GetTotalBytes(inBytes, outBytes);

		ULONGLONG nowTick = GetTickCount64();
		long long kbps = 0;

		if (m_send_prevTick != 0)
		{
			double elapsedSec = (nowTick - m_send_prevTick) / 1000.0;
			kbps = static_cast<long long>(((outBytes - m_send_prevOutOctets) / 1024.0) / elapsedSec);
		}

		m_send_prevOutOctets = outBytes;
		m_send_prevTick = nowTick;
		return kbps;
	}

	long long GetTotalRecvKBps()
	{
		unsigned long long inBytes, outBytes;
		GetTotalBytes(inBytes, outBytes);

		ULONGLONG nowTick = GetTickCount64();
		long long kbps = 0;

		if (m_recv_prevTick != 0)
		{
			double elapsedSec = (nowTick - m_recv_prevTick) / 1000.0;
			kbps = static_cast<long long>(((inBytes - m_recv_prevInOctets) / 1024.0) / elapsedSec);
		}

		m_recv_prevInOctets = inBytes;
		m_recv_prevTick = nowTick;
		return kbps;
	}

	float ProcessorTotal(void) { return m_processorTotal; }
	float ProcessorUser(void) { return m_processorUser; }
	float ProcessorKernel(void) { return m_processorKernel; }
	float ProcessTotal(void) { return m_processTotal; }
	float ProcessUser(void) { return m_processUser; }
	float ProcessKernel(void) { return m_processKernel; }
	void UpdateCpuTime(void)
	{
		//---------------------------------------------------------
		// 프로세서 사용률을 갱신한다.
		//
		// 본래의 사용 구조체는 FILETIME 이지만, ULARGE_INTEGER 와 구조가 같으므로 이를 사용함.
		// FILETIME 구조체는 100 나노세컨드 단위의 시간 단위를 표현하는 구조체임.
		//---------------------------------------------------------
		ULARGE_INTEGER Idle;
		ULARGE_INTEGER Kernel;
		ULARGE_INTEGER User;
		//---------------------------------------------------------
		// 시스템 사용 시간을 구한다.
		//
		// 아이들 타임 / 커널 사용 타임 (아이들포함) / 유저 사용 타임
		//---------------------------------------------------------
		if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
		{
			return;
		}
		// 커널 타임에는 아이들 타임이 포함됨.
		ULONGLONG KernelDiff = Kernel.QuadPart - m_processor_lastKernel.QuadPart;
		ULONGLONG UserDiff = User.QuadPart - m_processor_lastUser.QuadPart;
		ULONGLONG IdleDiff = Idle.QuadPart - m_processor_lastIdle.QuadPart;
		ULONGLONG Total = KernelDiff + UserDiff;
		ULONGLONG TimeDiff;

		if (Total == 0)
		{
			m_processorUser = 0.0f;
			m_processorKernel = 0.0f;
			m_processorTotal = 0.0f;
		}
		else
		{
			// 커널 타임에 아이들 타임이 있으므로 빼서 계산.
			m_processorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
			m_processorUser = (float)((double)UserDiff / Total * 100.0f);
			m_processorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);
		}
		m_processor_lastKernel = Kernel;
		m_processor_lastUser = User;
		m_processor_lastIdle = Idle;

		//---------------------------------------------------------
		// 지정된 프로세스 사용률을 갱신한다.
		//---------------------------------------------------------
		ULARGE_INTEGER None;
		ULARGE_INTEGER NowTime;
		//---------------------------------------------------------
		// 현재의 100 나노세컨드 단위 시간을 구한다. UTC 시간.
		//
		// 프로세스 사용률 판단의 공식
		//
		// a = 샘플간격의 시스템 시간을 구함. (그냥 실제로 지나간 시간)
		// b = 프로세스의 CPU 사용 시간을 구함.
		//
		// a : 100 = b : 사용률 공식으로 사용률을 구함.
		//---------------------------------------------------------
		//---------------------------------------------------------
		// 얼마의 시간이 지났는지 100 나노세컨드 시간을 구함,
		//---------------------------------------------------------
		GetSystemTimeAsFileTime((LPFILETIME)&NowTime);
		//---------------------------------------------------------
		// 해당 프로세스가 사용한 시간을 구함.
		//
		// 두번째, 세번째는 실행,종료 시간으로 미사용.
		//---------------------------------------------------------
		GetProcessTimes(m_process, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);
		//---------------------------------------------------------
		// 이전에 저장된 프로세스 시간과의 차를 구해서 실제로 얼마의 시간이 지났는지 확인.
		//
		// 그리고 실제 지나온 시간으로 나누면 사용률이 나옴.
		//---------------------------------------------------------
		TimeDiff = NowTime.QuadPart - m_process_lastTime.QuadPart;
		UserDiff = User.QuadPart - m_process_lastUser.QuadPart;
		KernelDiff = Kernel.QuadPart - m_process_lastKernel.QuadPart;
		Total = KernelDiff + UserDiff;

		m_processTotal = (float)(Total / (double)m_numberOfProcessors / (double)TimeDiff * 100.0f);
		m_processKernel = (float)(KernelDiff / (double)m_numberOfProcessors / (double)TimeDiff * 100.0f);
		m_processUser = (float)(UserDiff / (double)m_numberOfProcessors / (double)TimeDiff * 100.0f);
		m_process_lastTime = NowTime;
		m_process_lastKernel = Kernel;
		m_process_lastUser = User;
	}

private:
	unsigned long long m_send_prevInOctets = 0;
	unsigned long long m_send_prevOutOctets = 0;
	ULONGLONG m_send_prevTick = 0;
	unsigned long long m_recv_prevInOctets = 0;
	unsigned long long m_recv_prevOutOctets = 0;
	ULONGLONG m_recv_prevTick = 0;
	double GetCounterValue(PDH_HCOUNTER counter, DWORD format)
	{
		PDH_FMT_COUNTERVALUE val;
		PdhCollectQueryData(hQuery);
		if (PdhGetFormattedCounterValue(counter, format, NULL, &val) == ERROR_SUCCESS)
		{
			if (format == PDH_FMT_DOUBLE) return val.doubleValue;
			if (format == PDH_FMT_LONG) return (double)val.longValue;
			if (format == PDH_FMT_LARGE) return (double)val.largeValue;
		}
		return -1.0;
	}
	void GetTotalBytes(unsigned long long& inBytes, unsigned long long& outBytes)
	{
		MIB_IF_TABLE2* pIfTable = nullptr;
		if (GetIfTable2(&pIfTable) == NO_ERROR)
		{
			inBytes = 0;
			outBytes = 0;
			for (ULONG i = 0; i < pIfTable->NumEntries; i++)
			{
				inBytes += pIfTable->Table[i].InOctets;
				outBytes += pIfTable->Table[i].OutOctets;
			}
			FreeMibTable(pIfTable);
		}
	}



};