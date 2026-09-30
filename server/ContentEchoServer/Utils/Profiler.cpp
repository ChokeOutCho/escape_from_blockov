#include "Profiler.h"
#include <ctime>
#include <direct.h>

// static
LARGE_INTEGER Profiler::freqTime;
PROFILE_SAMPLES* Profiler::thread_samples[PROFILE_THREAD_MAX];
long Profiler::profileThreadCount;
long* Profiler::thread_resetFlags[PROFILE_THREAD_MAX];

// thread_local static
thread_local PROFILE_SAMPLES Profiler::samples;
thread_local long Profiler::initFlag;
thread_local long Profiler::resetFlag;

const char* text_line = "----------------------------------------------------------------------------------------------------------------------------+\0";

#pragma warning (push)
#pragma warning(disable:26495)
Profiler::Profiler(const char* tag)
{
#pragma warning (pop)
	if (initFlag == 0)
	{
		initFlag = 1;

		QueryPerformanceFrequency(&freqTime);
		{
			for (int i = 0; i < SAMPLE_MAX; i++)
			{
				samples.sample[i].Max[0] = 0;
				samples.sample[i].Max[1] = 0;
				samples.sample[i].Min[0] = LLONG_MAX;
				samples.sample[i].Min[1] = LLONG_MAX;
			}
			long count = InterlockedIncrement(&profileThreadCount) - 1;
			thread_samples[count] = &samples;
			thread_resetFlags[count] = &resetFlag;
		}

	}
	if (resetFlag == 1)
	{
		for (int i = 0; i < SAMPLE_MAX; i++)
		{
			PROFILE_SAMPLE* sample = &samples.sample[i];

			sample->Max[0] = 0;
			sample->Max[1] = 0;
			sample->Min[0] = LLONG_MAX;
			sample->Min[1] = LLONG_MAX;
			sample->Call = 0;
			sample->TotalTime = 0;

			resetFlag = 0;
		}
	}
	QueryPerformanceCounter(&startTime);
	m_tag = tag;
}

Profiler::~Profiler()
{

	QueryPerformanceCounter(&endTime);
	Update();
}

void Profiler::FlushToFile()
{
	time_t now = time(nullptr);
	struct tm timeInfo;
	localtime_s(&timeInfo, &now);

	// 디렉토리 이름: logs/YYYYMMDD
	char dirName[32];
	strftime(dirName, sizeof(dirName), "profiledata/%Y_%m", &timeInfo);

	// 디렉토리 생성 (존재하지 않으면)
#pragma warning(push)
#pragma warning(disable : 6031) // 반환값 안씀
	_mkdir("profiledata");        // 상위 디렉토리
	_mkdir(dirName);       // 날짜별 디렉토리
#pragma warning(pop)
	// 파일 이름: profile_YYYYMMDD_HHMMSS.txt
	char fileName[64];
	strftime(fileName, sizeof(fileName), "profile_%Y%m%d_%H%M%S.txt", &timeInfo);

	// 전체 경로 조합
	char fullPath[96];
	sprintf_s(fullPath, "%s/%s", dirName, fileName);

	// 파일 생성
	FILE* fp = nullptr;
	errno_t err = fopen_s(&fp, fullPath, "w");
	int e = GetLastError();
	if (fp == nullptr)
	{
		//printf("파일을 열 수 없습니다: %s\n", szFileName);
		return;
	}

	int cnt = 0;
	while (cnt < profileThreadCount)
	{
		PROFILE_SAMPLES* samples = thread_samples[cnt];
		fprintf_s(fp, "\n%s\n", text_line);
		fprintf_s(fp, "     ThreadID |                            Name |         Average |              Min |              Max |              Call |\n");
		fprintf_s(fp, "%s\n", text_line);
		for (int i = 0; i < samples->sampleCount; i++)
		{
			PROFILE_SAMPLE* sample = &samples->sample[i];
			__int64 sum_min_max =
				sample->Min[0] +
				sample->Min[1] +
				sample->Max[0] +
				sample->Max[1];
			double avg = (samples->sample[i].TotalTime - sum_min_max) * 1'000'000.0 / (freqTime.QuadPart) / (sample->Call - 4);
			fprintf_s(fp, " %12d |%32s |%14.4lf㎲ |%15.4lf㎲ |%15.4lf㎲ |%18llu |\n",
				samples->sample[i].threadID,
				sample->Tag,
				avg,
				sample->Min[0] * 1'000'000.0 / freqTime.QuadPart,
				sample->Max[0] * 1'000'000.0 / freqTime.QuadPart,
				sample->Call
			);
		}
		fprintf_s(fp, "%s\n", text_line);
		cnt++;
	}

	fclose(fp);

}

void Profiler::Reset()
{
	int cnt = 0;
	while (cnt < profileThreadCount)
	{
		*(thread_resetFlags[cnt]) = 1;
		cnt++;
	}


}


// Profile 구조체 배열에 데이터 갱신 및 생성
__inline void Profiler::Update()
{
	int cnt = 0;
	long isExist = 0;
	while (cnt < samples.sampleCount)
	{
		if (strcmp(samples.sample[cnt].Tag, m_tag) == 0)
		{
			isExist = 1;
			break;
		}
		cnt++;
	}

	PROFILE_SAMPLE* sample = &samples.sample[cnt];

	__int64 time = endTime.QuadPart - startTime.QuadPart;

	strcpy_s(sample->Tag, strlen(m_tag) + 1, m_tag);

	sample->TotalTime += time;
	// min 
	if (time < sample->Min[0])
	{
		sample->Min[0] = time;
	}
	else if (time < sample->Min[1])
	{
		sample->Min[1] = time;
	}
	// max
	if (time > sample->Max[0])
	{
		sample->Max[0] = time;
	}
	else if (time > sample->Max[1])
	{
		sample->Max[1] = time;
	}
	sample->Call++;
	if (isExist == 0)
	{
		sample->threadID = GetCurrentThreadId();
		samples.sampleCount++;
	}
}