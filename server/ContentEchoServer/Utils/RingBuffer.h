#pragma once
#include <iostream>
class RingBuffer
{
public:
	RingBuffer(int size);
	~RingBuffer(void);

	/// <summary>버퍼의 크기를 변경한다.</summary>
	/// <param name = "size">버퍼를 재조정할 크기</param>
	/// <returns>재조정된 버퍼 크기</returns>
	//int Resize(int size);

	/// <summary>버퍼의 크기를 얻는다.</summary>
	/// <param></param>
	/// <returns>버퍼의 크기</returns>
	int	GetBufferSize(void) const;

	/// <summary>현재 사용 중인 용량을 얻는다.</summary>
	/// <param></param>
	/// <returns>현재 사용 중인 용량</returns>
	__inline int	GetUseSize() const
	{
		char* front = mFront;
		char* rear = mRear;
		return GetUseSize(front, rear);
	}

	/// <summary>현재 사용 중인 용량을 원자적으로 얻는다.</summary>
	/// <param></param>
	/// <returns>현재 사용 중인 용량</returns>
	__inline int	GetUseSize(char* front, char* rear) const
	{
		int ret = 0;
		if (front > rear)
		{
			ret = int((mBuffer + mBufferSize - front) + (rear - mBuffer));
			return ret;
		}
		else
		{
			ret = int(rear - front);
			return ret;
		}
	}

	/// <summary>현재 버퍼에 남은 용량을 얻는다.</summary>
	/// <param></param>
	/// <returns>현재 버퍼에 남은 용량</returns>
	int	GetFreeSize() const
	{
		char* front = mFront;
		char* rear = mRear;
		return mBufferSize - GetUseSize(front, rear) - 1;
	}
	/// <summary>현재 버퍼에 남은 용량을 얻는다.</summary>
	/// <param></param>
	/// <returns>현재 버퍼에 남은 용량</returns>
	int	GetFreeSize(char* front, char* rear) const
	{
		return mBufferSize - GetUseSize(front, rear) - 1;
	}

	/// <summary>버퍼에 데이터를 삽입하고 WirtePos를 이동시킨다. 
	/// 이 버퍼는 남은 용량보다 삽입을 시도하는 데이터의 용량이 더 크다면
	/// 삽입을 아예 시도하지 않기 때문에
	/// 0 또는 삽입을 시도한 값만 리턴한다.</summary>
	/// <param name = "pData">삽입할 데이터 포인터</param>
	/// <param name = "size">삽입할 데이터 크기</param>
	/// <returns>삽입 성공한 크기</returns>
	int	Enqueue(const char* pData, const int size);

	/// <summary>버퍼에서 데이터를 추출하고 ReadPos를 이동시킨다.</summary>
	/// <param name = "pDest">데이터를 추출받을 위치</param>
	/// <param name = "size">추출받을 데이터 크기</param>
	/// <returns>추출 성공한 크기</returns>
	__inline int Dequeue(char* pDest, int size)
	{
		char* front = mFront;
		char* rear = mRear;

		// 사용 중인 공간보다 크면 최대한 읽기
		int readSize = size;
		if (readSize > GetUseSize(front, rear))
			readSize = GetUseSize(front, rear);

		int directSize = DirectDequeueSize(front, rear);
		if (readSize <= directSize)
		{
			memcpy(pDest, front, readSize);
			mFront = mBuffer + ((front - mBuffer + readSize) % mBufferSize);
		}
		else
		{
			memcpy(pDest, front, directSize);
			memcpy(pDest + directSize, mBuffer, readSize - directSize);
			mFront = mBuffer + ((readSize - directSize) % mBufferSize);
		}

		return readSize;
	}
	/// <summary>ReadPos에서 데이터를 읽어온다. ReadPos의 위치는 변하지 않는다.</summary>
	/// <param name = "pDest">데이터를 복사받을 위치</param>
	/// <param name = "size">읽을 데이터 크기</param>
	/// <returns>읽기 성공한 크기</returns>
	__inline int Peek(char* pDest, int size) const
	{
		char* front = mFront;
		char* rear = mRear;
		// 사용 중인 공간보다 크면 최대한 읽기
		int readSize = size;
		if (readSize > GetUseSize(front, rear))
			readSize = GetUseSize(front, rear);

		int directSize = DirectDequeueSize(front, rear);
		if (readSize <= directSize)
		{
			memcpy(pDest, front, readSize);
		}
		else
		{
			memcpy(pDest, front, directSize);
			memcpy(pDest + directSize, mBuffer, readSize - directSize);
		}

		return readSize;
	}

	/// <summary>버퍼의 모든 데이터를 삭제한다.</summary>
	/// <param></param>
	/// <returns></returns>
	void ClearBuffer(void);

	/// <summary>
	/// 버퍼 포인터로 외부에서 한 번에 읽을 수 있는 길이이다.
	/// 원형 큐의 구조상 버퍼 끝단의 데이터가 버퍼의 시작 위치의 데이터와 이어질 수 있다.
	/// 이러한 경우에는 두 번에 걸쳐 데이터를 얻어야 할 것이다.
	/// 즉, 끝과 시작의 경계까지 남은 길이를 의미한다.
	/// </summary>
	 /// <returns>한번에 사용 가능한 크기</returns>
	__inline int DirectEnqueueSize(char* front, char* rear) const
	{
		// Rear가 Front보다 뒤에 있다면
		// Rear부터 버퍼 끝까지의 길이를,
		if (rear >= front)
		{
			if (front == mBuffer)
			{
				return (int)(mBuffer + mBufferSize - rear) - 1;
			}
			else
			{
				return (int)(mBuffer + mBufferSize - rear);

			}
		}
		// 아니라면 현재 프리인 공간을 반환한다. (rear부터 front까지의 길이)
		else
			return (int)(front - rear - 1);
	}
	/// <summary>
	/// 버퍼 포인터로 외부에서 한 번에 읽을 수 있는 길이이다.
	/// 원형 큐의 구조상 버퍼 끝단의 데이터가 버퍼의 시작 위치의 데이터와 이어질 수 있다.
	/// 이러한 경우에는 두 번에 걸쳐 데이터를 삽입해야 할 것이다.
	/// 즉, 끝과 시작의 경계까지 남은 길이를 의미한다.
	/// </summary>
	/// <returns>한번에 사용 가능한 크기</returns>
	__inline int	DirectDequeueSize(char* front, char* rear) const
	{

		// Front가 Rear보다 뒤에 있다면
		// Front부터 버퍼 끝까지의 길이를,
		if (front > rear)
			return int(mBuffer + mBufferSize - front);
		// 아니라면 현재 사용 중인 공간을 반환한다. (front부터 rear까지의 길이)
		else
			return int(rear - front);
	}

	/// <summary>버퍼의 Rear 포인터를 이동한다.</summary>
	/// <param name = "size">이동할 크기</param>
	/// <returns>실제 이동한 크기</returns>
	int	MoveRear(int size);

	/// <summary>버퍼의 Front 포인터를 이동한다.</summary>
	/// <param name = "size">이동할 크기</param>
	/// <returns>실제 이동한 크기</returns>
	__inline int MoveFront(int size)
	{
		char* front = mFront;
		char* rear = mRear;
		// 사용 중인 공간보다 크면 최대한 읽기
		int moveSize = size;
		if (moveSize > GetUseSize(front, rear))
			moveSize = GetUseSize(front, rear);

		int directSize = DirectDequeueSize(front, rear);
		if (moveSize <= directSize)
		{
			mFront = mBuffer + ((front - mBuffer + moveSize) % mBufferSize);

		}
		else
		{
			mFront = mBuffer + ((moveSize - directSize) % mBufferSize);
		}

		return moveSize;
	}
	/// <summary>버퍼의 Front 포인터를 얻는다.</summary>
	/// <param></param>
	/// <returns>버퍼의 Front 포인터</returns>
	char* GetFrontPtr(void) const;

	/// <summary>버퍼의 Rear 포인터를 얻는다.</summary>
	/// <returns>버퍼의 Rear 포인터</returns>
	char* GetRearPtr(void) const;

	char* GetBufferPtr() const
	{
		return mBuffer;
	}

private:
	char* mBuffer;
	int mBufferSize;
	char* mFront;
	char* mRear;

};