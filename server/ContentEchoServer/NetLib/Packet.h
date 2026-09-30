#pragma once
#include <windows.h>
#include "../Utils/ObjectPool.h"
#include "../Utils/TLSObjectPool.h"
#include "../Utils/Profiler.h"
#include <list>
#include "NetLibraryProtocol.h"
// 패킷 페이로드(Type 포함) 최대 크기. NetHeader(5B)는 제외한 값
#define PAYLOAD_LEN_DEFAULT 512
// 직렬화 버퍼 크기 = NetHeader + 최대 페이로드 (NetAlloc이 앞 5B를 헤더로 예약하므로)
#define PACKET_BUFFER_SIZE (PAYLOAD_LEN_DEFAULT + (int)sizeof(NetHeader))
enum class eType
{
	SENDPACKET,
	SENDPACK_MULTICAST,
	RECVPROC,
	LOGIN,
	SENDPOST,
	SENDPACKETFAST,
	NETALLOC,
	ALLOC,
};
class SerializeBuffer
{
public:

	/// <summary>
	/// Packet Enum
	/// </summary>
	enum class ePACKET
	{
		BUFFER_DEFAULT = 1400,		// 패킷의 기본 버퍼 사이즈.
		BUFFER_MAX = 10000,
	};

	enum class eErrorCode
	{
		NONE = 0,
		FAILED_WRITE = 1,
		FAILED_READ = 2,
	};

	/// <summary>
	/// 생성자
	/// </summary>
	SerializeBuffer();
	SerializeBuffer(int bufferSize);

	/// <summary>
	/// 소멸자
	/// </summary>
	~SerializeBuffer();


	/// <summary>
	/// 버퍼를 비운다.
	/// </summary>
	/// <param name=""></param>
	__inline void Clear()
	{
		mFront = mBuffer;
		mRear = mBuffer;
		mDataSize = 0;
		//lastError = eErrorCode::NONE;
	}

	/// <summary>
	/// 버퍼 사이즈 얻기
	/// </summary>
	/// <param name=""></param>
	/// <returns></returns>
	int		GetBufferSize(void) { return mBufferSize; }

	/// <summary>
	/// 현재 사용 중인 크기 얻기
	/// </summary>
	/// <param name=""></param>
	/// <returns>현재 사용 중인 크기</returns>
	int		GetDataSize(void) { return mDataSize; }

	/// <summary>
	/// 버퍼 포인터 얻기
	/// </summary>
	/// <param name=""></param>
	/// <returns>버퍼 포인터</returns>
	__inline char* GetBufferPtr(void) { return mBuffer; }
	char* GetBufferFrontPtr(void) { return mFront; }

	/// <summary>
	/// 버퍼의 pos를 이동한다. 단, 음수 이동은 금지한다.
	/// GetBufferPtr()을 이용하여 외부에서 강제로 버퍼 내용을 수정할 필요가 있는 경우에 사용한다.
	/// </summary>
	/// <param name="size">크기</param>
	/// <returns></returns>
	int	MoveRear(int size);

	/// <summary>
	/// 버퍼의 pos를 이동한다. 단, 음수 이동은 금지한다.
	/// GetBufferPtr()을 이용하여 외부에서 강제로 버퍼 내용을 수정할 필요가 있는 경우에 사용한다.
	/// </summary>
	/// <param name="size"></param>
	/// <returns></returns>
	int	MoveFront(int size);

	/// <summary>
	/// 대입
	/// </summary>
	/// <param name="clSrcPacket"></param>
	/// <returns></returns>
	SerializeBuffer& operator = (SerializeBuffer& srcPacket);

	/// <summary>
	/// uchar 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (unsigned char value);

	/// <summary>
	/// char 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (char value);

	/// <summary>
	/// short 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (short value);

	/// <summary>
	/// ushort 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (unsigned short value)
	{
		if (mBufferEnd < mRear + sizeof(unsigned short))
		{
			//lastError = eErrorCode::FAILED_WRITE;
		}
		else
		{
			*(unsigned short*)mRear = value;
			mRear += sizeof(unsigned short);
			mDataSize += sizeof(unsigned short);
		}

		return *this;
	}
	/// <summary>
	/// int 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (int value);

	/// <summary>
	/// long 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (long value);

	/// <summary>
	/// float 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (float value);

	/// <summary>
	/// __int64 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	__inline SerializeBuffer& operator << (__int64 value)
	{
		if (mBufferEnd < mRear + sizeof(__int64))
		{
			//lastError = eErrorCode::FAILED_WRITE;
		}
		else
		{
			*(__int64*)mRear = value;
			mRear += sizeof(__int64);
			mDataSize += sizeof(__int64);
		}

		return *this;
	}
	/// <summary>
	/// double 삽입
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator << (double value);

	SerializeBuffer& operator << (unsigned long long value);
	SerializeBuffer& operator << (DWORD value);

	/// <summary>
	/// BYTE(uchar) 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (BYTE& value);

	/// <summary>
	/// char 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (char& value);

	/// <summary>
	/// short 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (short& value);

	/// <summary>
	/// WORD(ushort) 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (WORD& value);

	/// <summary>
	/// int 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (int& value);

	/// <summary>
	/// DWORD(ulong)추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (DWORD& value);

	/// <summary>
	/// float 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (float& value);

	/// <summary>
	/// __int64 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (__int64& value);

	/// <summary>
	/// double 추출
	/// </summary>
	/// <param name="value"></param>
	/// <returns></returns>
	SerializeBuffer& operator >> (double& value);

	SerializeBuffer& operator >> (unsigned long long& value);
	/// <summary>
	/// 데이터를 얻는다.
	/// </summary>
	/// <param name="dest">데이터를 복사할 포인터</param>
	/// <param name="size">크기</param>
	/// <returns>얻는데 성공한 크기</returns>
	int		GetData(char* dest, int size);

	/// <summary>
	/// 데이터를 삽입한다.
	/// </summary>
	/// <param name="src">삽입할 데이터 포인터</param>
	/// <param name="size">크기</param>
	/// <returns>삽입에 성공한 크기</returns>
	__inline int	PutData(char* src, int size)
	{
		if (mDataSize + size > mBufferSize)
		{
			//lastError = eErrorCode::FAILED_WRITE;
			size = 0;
		}
		else
		{
			memcpy(mRear, src, size);
			mRear += size;
			mDataSize += size;
		}

		return size;
	}

	/// <summary>
	/// 마지막 오류를 반환한다.
	/// </summary>
	/// <returns>1: 쓰기오류, 2: 읽기오류</returns>
	//int		GetLastError() { return (int)lastError; }

	/// <summary>
	/// 버퍼 사이즈를 두 배로 확장한다.
	/// 주의사항: 테스트가 부족한 상태이다.
	/// </summary>
	/// <param name="resize"></param>
	/// <returns>리사이즈 된 사이즈</returns>
	int Expand();


protected:
	char* mRear;	
	char* mBuffer;
	char* mBufferEnd;
	char* mFront;
	int	mBufferSize;
	int	mDataSize;
	//eErrorCode lastError;
};

class Packet : public SerializeBuffer
{
public:
	long refCount;
	eType type;
	friend class ObjectPool_TLSPoolComponent<Packet>;
	inline static TLSObjectPool<Packet> pool{ 256 };

#pragma warning(push)
#pragma warning(disable : 26495) // 초기화 경고 억제. 사용시에 세팅하기 때문에

	//unsigned long long sessionHandle;
	NetHeader* headerPtr;
	unsigned char* payloadPtr;
	inline static std::list<Packet*> debuglist;
	inline static SRWLOCK lock_debuglist;


	__forceinline static Packet* Alloc()
	{
		Packet* packet = pool.Alloc();
		packet->Clear();
		packet->refCount = 0;
		//InterlockedExchange(&packet->refCount, 0);

		//AcquireSRWLockExclusive(&lock_debuglist);
		//debuglist.push_back(packet);
		//ReleaseSRWLockExclusive(&lock_debuglist);
		return packet;


	}
	__forceinline static void Free(Packet* packet)
	{
		//AcquireSRWLockExclusive(&lock_debuglist);
		//debuglist.remove(packet);
		//ReleaseSRWLockExclusive(&lock_debuglist);
		pool.Free(packet);
	}
	__forceinline static Packet* NetAlloc()
	{
		NetHeader emptyHeader = {}; // 암호화 미사용 시 RandKey/CheckSum은 0으로 송신
		Packet* packet = pool.Alloc();
		packet->Clear();

		packet->PutData((char*)&emptyHeader, sizeof(NetHeader));
		packet->headerPtr = (NetHeader*)packet->GetBufferPtr();
		packet->payloadPtr = (unsigned char*)packet->headerPtr + sizeof(NetHeader);

		packet->refCount = 0;

		//InterlockedExchange(&packet->refCount, 0);
		//AcquireSRWLockExclusive(&lock_debuglist);
		//debuglist.push_back(packet);
		//ReleaseSRWLockExclusive(&lock_debuglist);
		return packet;
	}

	__forceinline static void NetFree(Packet* packet)
	{
		long interRet = InterlockedDecrement(&packet->refCount);
		if (interRet == 0)
			pool.Free((Packet*)packet);
		// 디버그용
		//else if(interRet < 0)
		//	printf("중복시도감지");

		//AcquireSRWLockExclusive(&lock_debuglist);
		//debuglist.remove(packet);
		//ReleaseSRWLockExclusive(&lock_debuglist);
		pool.Free(packet);
	}

	__inline static int GetPoolUseSize()
	{
		return pool.GetUseSize();
	}

	__inline NetHeader* GetHeaderPtr()
	{
		return headerPtr;
	}

	__forceinline unsigned char* GetPayloadPtr()
	{
		return payloadPtr;
	}

	__inline int GetPayloadSize()
	{
		return GetDataSize() - sizeof(NetHeader);
	}
private:
	Packet() {}

};
#pragma warning(pop)