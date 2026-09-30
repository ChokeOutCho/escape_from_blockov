#pragma once

//#define PROFILE

#include <iostream>
#include <windows.h>
#define SAMPLE_MAX 128
#define PROFILE_THREAD_MAX 128

#define STRCAT(x, y) x##y
#ifdef PROFILE
#define PROFILING(tag) Profiler STRCAT(_profiler_, __COUNTER__)(tag)
#else
#define PROFILING(tag) 
#endif

struct PROFILE_SAMPLE
{
	long			UseFlag;			// 프로파일의 사용 여부. (배열시에만)
	CHAR			Tag[64];			// 프로파일 샘플 이름.

	LARGE_INTEGER	StartTime;			// 프로파일 샘플 실행 시간.

	__int64			TotalTime;			// 전체 사용시간 카운터 Time.	(출력시 호출회수로 나누어 평균 구함)
	__int64			Min[2];				// 최소 사용시간 카운터 Time.	(초단위로 계산하여 저장 / [0] 가장최소 [1] 다음 최소 [2])
	__int64			Max[2];				// 최대 사용시간 카운터 Time.	(초단위로 계산하여 저장 / [0] 가장최대 [1] 다음 최대 [2])

	__int64			Call;				// 누적 호출 횟수.
	unsigned long	threadID;
};

struct PROFILE_SAMPLES
{
	int sampleCount;
	PROFILE_SAMPLE sample[SAMPLE_MAX];
};

class Profiler
{
public:
	Profiler(const char* tag);
	~Profiler();

	static void FlushToFile();
	static void Reset();

private:
	static thread_local long initFlag;
	static LARGE_INTEGER freqTime;
	static thread_local PROFILE_SAMPLES samples;

	__inline void Update();

	static long profileThreadCount;
	static PROFILE_SAMPLES* thread_samples[PROFILE_THREAD_MAX];
private:
	LARGE_INTEGER startTime;
	LARGE_INTEGER endTime;
	const char* m_tag;
	static long* thread_resetFlags[PROFILE_THREAD_MAX];
	static thread_local long resetFlag;
};
