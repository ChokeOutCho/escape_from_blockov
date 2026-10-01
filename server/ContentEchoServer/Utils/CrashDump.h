#pragma once
////////////////////////////////////////////////////////////////////////
// 크래시 덤프
//  - 이 헤더를 include하면 정적 객체가 프로그램 시작 시 처리기를 등록한다.
//  - 잡는 것: 처리되지 않은 SEH 예외(SetUnhandledExceptionFilter), CRT 잘못된 인자·순수 가상 호출·CRT 보고,
//            abort()(SIGABRT), std::terminate(처리되지 않은 C++ 예외 포함)
//  - 파일: 작업 폴더 dumps/Dump_YYYYMMDD_HHMMSS_<pid>.dmp (전체 메모리 덤프). 프로세스당 1회만 쓴다.
//  - SetOnCrash(콜백): 덤프 직전에 호출 (예: 게임 로그에 한 줄 남기기). 콜백 안에서 잠금을 기다리지 말 것.
//  - CrashDump::Crash(): 일부러 크래시 (테스트용)
////////////////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <windows.h>
#include <psapi.h>
#include <ctime>
#include <direct.h>
#include <iostream>
#include <csignal>
#include <exception>
#include <DbgHelp.h>
#include <crtdbg.h>
#pragma comment(lib,"Dbghelp.lib")

class CrashDump
{
public:
	// code: SEH 예외 코드, reason: "exception" / "abort" / "terminate" / "invalid-parameter" / "pure-call" / "crt-report" / "manual"
	// (abort 등은 이 처리기가 일부러 접근 위반을 일으켜 덤프하므로 code는 0xC0000005)
	typedef void (*CrashCallback)(unsigned long code, const char* reason, const char* dumpPath);

	CrashDump()
	{
		m_dumpCount = 0;

		_set_invalid_parameter_handler(myInvalidParameterHandler);
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);
		_CrtSetReportHook(_custom_Report_hook);

		// 퓨어콜 핸들러 우회
		_set_purecall_handler(myPureCallHandler);
		// abort(): 기본 메시지 창 대신 덤프
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		signal(SIGABRT, mySignalHandler);
		// 처리되지 않은 C++ 예외 등
		std::set_terminate(myTerminateHandler);
		SetHandlerDump();
	}

	static void SetOnCrash(CrashCallback cb) { s_onCrash = cb; }

	static void Crash(void)
	{
		if (s_reason == nullptr) s_reason = "manual";
		int* p = nullptr;
#pragma warning(push)
#pragma warning(disable : 6011) // 일부러 터뜨리는 것
		*p = 0;
#pragma warning(pop)
	}

	static inline long m_dumpCount = 0;
	static inline CrashCallback s_onCrash = nullptr;
	static inline const char* s_reason = nullptr;

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		WriteDump(pExceptionPointer, pExceptionPointer && pExceptionPointer->ExceptionRecord ? pExceptionPointer->ExceptionRecord->ExceptionCode : 0);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// 덤프 파일 쓰기 (프로세스당 1회. 다른 스레드가 동시에 죽으면 먼저 온 쪽만 쓰고 나머지는 대기)
	static void WriteDump(PEXCEPTION_POINTERS pExceptionPointer, unsigned long code)
	{
		if (InterlockedIncrement(&m_dumpCount) != 1)
		{
			Sleep(INFINITE);
			return;
		}

		time_t now = time(nullptr);
		struct tm timeInfo;
		localtime_s(&timeInfo, &now);
		_mkdir("dumps");
		char stamp[32];
		strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &timeInfo);
		char path[96];
		sprintf_s(path, "dumps\\Dump_%s_%lu.dmp", stamp, GetCurrentProcessId());

		const char* reason = s_reason ? s_reason : "exception";
		if (s_onCrash) s_onCrash(code, reason, path);

		printf("\n\n!!! Crash (0x%08lX, %s) !!!\n Save Dump.. %s\n", code, reason, path);
		HANDLE dumpFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (dumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION info;
			info.ThreadId = GetCurrentThreadId();
			info.ExceptionPointers = pExceptionPointer;
			info.ClientPointers = TRUE;
			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, MiniDumpWithFullMemory,
			                  pExceptionPointer ? &info : NULL, NULL, NULL);
			CloseHandle(dumpFile);
			printf("CrashDump Save Finish!\n");
		}
	}

	static void SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	// Invalid Parameter handler
	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
	{
		s_reason = "invalid-parameter";
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue)
	{
		s_reason = "crt-report";
		Crash();
		return true;
	}

	static void myPureCallHandler()
	{
		s_reason = "pure-call";
		Crash();
	}

	// abort() / std::terminate: 예외 정보가 없으므로 여기서 직접 크래시를 일으켜 현재 스택을 담은 SEH 덤프로 남긴다
	static void mySignalHandler(int)
	{
		s_reason = "abort";
		Crash();
	}

	static void myTerminateHandler()
	{
		s_reason = "terminate";
		Crash();
	}

	static CrashDump instance;
};


inline CrashDump CrashDump::instance;
