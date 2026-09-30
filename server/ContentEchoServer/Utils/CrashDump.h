#pragma once
#include <stdlib.h>
#include <windows.h>
#include <psapi.h>
#include <ctime>
#include <direct.h>
#include <iostream>
#include <DbgHelp.h>
#include <crtdbg.h>
#pragma comment(lib,"Dbghelp.lib")
class CrashDump
{
public:

	CrashDump()
	{
		m_dumpCount = 0;

		_invalid_parameter_handler oldHandler, newHandler;
		newHandler = myInvalidParameterHandler;

		oldHandler = _set_invalid_parameter_handler(newHandler);
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);

		_CrtSetReportHook(_custom_Report_hook);

		/*
			퓨어콜 핸들러 우회
		*/
		_set_purecall_handler(myPureCallHandler);
		SetHandlerDump();
	}

	static void Crash(void)
	{
		int* p = nullptr;
#pragma warning(push)
#pragma warning(disable : 6011) // 일부로 터치는거임
		*p = 0;
#pragma warning(pop)
	}

	static long m_dumpCount;

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		SYSTEMTIME nowTime;
		long dumpCount = InterlockedIncrement(&m_dumpCount);

		// 현재 날짜와 시간 가져오기
		time_t now = time(nullptr);
		struct tm timeInfo;
		localtime_s(&timeInfo, &now);

		// 파일 이름: profile_YYYYMMDD_HHMMSS.txt
		wchar_t fileName[64];
		wcsftime(fileName, sizeof(fileName) / sizeof(wchar_t), L"Dump_%Y_%m_%d.dmp", &timeInfo);

		wprintf(L"\n\n\n\n\n\n!!! Crash !!!\n Save Dump..\n");
		HANDLE dumpFile = CreateFile(fileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (dumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION minidumpExceptionInformation;
			minidumpExceptionInformation.ThreadId = GetCurrentThreadId();
			minidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
			minidumpExceptionInformation.ClientPointers = TRUE;

			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, MiniDumpWithFullMemory, &minidumpExceptionInformation, NULL, NULL);
			CloseHandle(dumpFile);
			wprintf(L"CrashDump Save Finish!");

		}

		return EXCEPTION_EXECUTE_HANDLER;

	}

	static void SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	//Invalid Parameter handler
	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
	{
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue)
	{
		Crash();
		return true;
	}
	static void myPureCallHandler()
	{
		Crash();
	}
	static CrashDump instance;
};

long CrashDump::m_dumpCount;
CrashDump CrashDump::instance;