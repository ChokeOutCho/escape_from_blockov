#include "RingBuffer.h"


int RingBuffer::GetBufferSize() const { return mBufferSize; }
char* RingBuffer::GetFrontPtr() const { return mFront; }
char* RingBuffer::GetRearPtr() const { return mRear; }

int RingBuffer::Enqueue(const char* pData, int size)
{
	char* front = mFront;
	char* rear = mRear;

	if (size > GetFreeSize(front, rear)) return 0;

	int directSize = DirectEnqueueSize(front, rear);
	if (size <= directSize)
	{
		memcpy(rear, pData, size);
		mRear = mBuffer + ((rear - mBuffer + size) % mBufferSize);
	}
	else
	{
		memcpy(rear, pData, directSize);
		memcpy(mBuffer, pData + directSize, size - directSize);
		mRear = mBuffer + ((size - directSize) % mBufferSize);
	}

	return size;
}


//int RingBuffer::Resize(int size)
//{
//	if (size == 0) size = 1;
//	char* front = mFront;
//	char* rear = mRear;
//	char* temp = new char[size];
//	int out = Peek(temp, size);
//	delete[] mBuffer;
//	mBuffer = temp;
//	mBufferSize = size;
//	ClearBuffer();
//	MoveRear(out);
//
//	return mBufferSize;
//}

void RingBuffer::ClearBuffer()
{
	mFront = mBuffer;
	mRear = mBuffer;
}


int RingBuffer::MoveRear(int size)
{
	char* front = mFront;
	char* rear = mRear;
	// 남은 공간보다 크면 이동 불가
	if (size > (GetFreeSize(front, rear))) return 0;

	int directSize = DirectEnqueueSize(front, rear);
	if (size <= directSize)
	{
		mRear = mBuffer + ((rear - mBuffer + size) % mBufferSize);
	}
	else
	{
		mRear = mBuffer + ((size - directSize) % mBufferSize);
	}
	return size;
}



RingBuffer::RingBuffer(int size) : mBufferSize(size)
{
	mBuffer = new char[size];
	mFront = mBuffer;
	mRear = mBuffer;
}

RingBuffer::~RingBuffer()
{
	delete[] mBuffer;
}