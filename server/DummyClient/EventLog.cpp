#include "Common.h"
#include "EventLog.h"
#include <cstdarg>
#include <ctime>

EventLog g_eventLog;

namespace
{
	SRWLOCK g_logLock = SRWLOCK_INIT;

	void TimeStamp(char* buf, size_t n)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}
}

bool EventLog::Open(const std::string& header)
{
	CreateDirectoryA("logs", nullptr);
	SYSTEMTIME st;
	GetLocalTime(&st);
	char name[128];
	snprintf(name, sizeof(name), "logs\\dummy_%04d%02d%02d_%02d%02d%02d.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	m_path = name;
	if (fopen_s(&m_fp, name, "a") != 0) { m_fp = nullptr; return false; }
	char ts[64];
	TimeStamp(ts, sizeof(ts));
	fprintf(m_fp, "# %s DummyClient 비정상 이벤트 로그\n# %s\n", ts, header.c_str());
	fprintf(m_fp, "# 형식: 시각 [종류] 더미 이름 id=플레이어ID 상태 원인 (오류코드 설명) kick=사유 접속유지ms 마지막수신후ms 위치\n");
	fflush(m_fp);
	return true;
}

void EventLog::Close()
{
	AcquireSRWLockExclusive(&g_logLock);
	if (m_fp) { fflush(m_fp); fclose(m_fp); m_fp = nullptr; }
	ReleaseSRWLockExclusive(&g_logLock);
}

void EventLog::Write(const char* type, const char* fmt, ...)
{
	char msg[768];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char ts[64];
	TimeStamp(ts, sizeof(ts));

	AcquireSRWLockExclusive(&g_logLock);
	if (m_fp) fprintf(m_fp, "%s [%s] %s\n", ts, type, msg);
	ReleaseSRWLockExclusive(&g_logLock);
}

void EventLog::Flush()
{
	AcquireSRWLockExclusive(&g_logLock);
	if (m_fp) fflush(m_fp);
	ReleaseSRWLockExclusive(&g_logLock);
}

std::string EventLog::ErrorText(int err)
{
	if (err == 0) return "";
	char buf[256] = {};
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)err, 0, buf, sizeof(buf), nullptr);
	while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '.')) buf[--n] = 0;
	return n ? std::string(buf) : std::string("unknown");
}
