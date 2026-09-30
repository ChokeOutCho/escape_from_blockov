#pragma once

#include <windows.h>
#include <strsafe.h>
#include <fstream>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <sstream>


#define SYSLOG_DIRECTORY(x) Logger::Instance().SetDirectory(x)
#define SYSLOG_LEVEL(x)     Logger::Instance().SetLevel(x)
#define LOG(type, level, fmt, ...) Logger::Instance().Log(type, level, fmt, __VA_ARGS__)
#define LOGHEX(type, level, msg, data, len) Logger::Instance().LogHex(type, level, MSG_CONTENT, data, len)

enum LOG_LEVEL
{
	LEVEL_DEBUG = 0,
	LEVEL_ERROR = 1,
	LEVEL_SYSTEM = 2
};

class Logger
{
public:

	static Logger& Instance()
	{
		static Logger instance;
		return instance;
	}

	void SetDirectory(const wchar_t* dir)
	{
		EnterCriticalSection(&m_cslock);
		StringCchCopyW(m_logDirectory, MAX_PATH, dir);
		CreateDirectoryIfNotExists(m_logDirectory);
		LeaveCriticalSection(&m_cslock);
	}

	void SetLevel(LOG_LEVEL level)
	{
		m_logLevel = level;
	}

	void Log(const wchar_t* szType, LOG_LEVEL level, const wchar_t* szFormat, ...)
	{
		if (level < m_logLevel) return;

		WCHAR szMessage[1024] = { 0 };
		va_list args;
		va_start(args, szFormat);
		HRESULT hr = StringCchVPrintfW(szMessage, 1024, szFormat, args);
		va_end(args);
		if (FAILED(hr)) return;

		SYSTEMTIME st;
		GetLocalTime(&st);

		WCHAR szTime[32] = { 0 };
		StringCchPrintfW(szTime, 32, L"%04d-%02d-%02d %02d:%02d:%02d",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		WCHAR szFileName[256] = { 0 };
		StringCchPrintfW(szFileName, 256, L"%s\\%04d%02d_%s.txt",
			m_logDirectory, st.wYear, st.wMonth, szType);

		DWORD count = InterlockedIncrement(&m_logCounter);

		WCHAR szFullLine[2048] = { 0 };
		StringCchPrintfW(szFullLine, 2048, L"[%s] [%s / %s / %08u] %s\n",
			szType, szTime, LevelToString(level), count, szMessage);

		// 콘솔 출력
		//wprintf(L"%s", szFullLine);

		// 파일 저장
		EnterCriticalSection(&m_cslock);
		FILE* fp = nullptr;
		_wfopen_s(&fp, szFileName, L"a+, ccs=UTF-8");
		if (fp)
		{
			fputws(szFullLine, fp);
			fclose(fp);
		}
		LeaveCriticalSection(&m_cslock);
	}

	void LogHex(const wchar_t* szType, LOG_LEVEL level, const wchar_t* szLog, BYTE* pData, int len)
	{
		if (level < m_logLevel) return;

		std::wstringstream ss;
		ss << szLog << L" [HEX] ";
		for (int i = 0; i < len; ++i)
		{
			ss << std::hex << std::uppercase << std::setw(2) << std::setfill(L'0') << (int)pData[i] << L" ";
		}

		Log(szType, level, L"%s", ss.str().c_str());
	}

private:
	Logger() : m_logLevel(LEVEL_DEBUG), m_logCounter(0)
	{
		InitializeCriticalSection(&m_cslock);
		StringCchCopyW(m_logDirectory, MAX_PATH, L".\\syslogs");
		CreateDirectoryIfNotExists(m_logDirectory);
	}

	~Logger()
	{
		DeleteCriticalSection(&m_cslock);
	}

	void CreateDirectoryIfNotExists(const wchar_t* dir)
	{
		DWORD attr = GetFileAttributesW(dir);
		if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
		{
			CreateDirectoryW(dir, nullptr);
		}
	}

	const wchar_t* LevelToString(LOG_LEVEL level)
	{
		switch (level)
		{
		case LEVEL_DEBUG: return L"DEBUG";
		case LEVEL_ERROR: return L"ERROR";
		case LEVEL_SYSTEM: return L"SYSTEM";
		default: return L"UNKNOWN";
		}
	}

	wchar_t m_logDirectory[MAX_PATH];
	LOG_LEVEL m_logLevel;
	CRITICAL_SECTION m_cslock;

	LONG m_logCounter;
};