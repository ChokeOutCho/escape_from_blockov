#include "Packet.h"

SerializeBuffer::SerializeBuffer()
{
	int bufferSize = PACKET_BUFFER_SIZE;
	mBuffer = new char[bufferSize + 1];
	mBufferSize = bufferSize;
	mDataSize = 0;
	mBufferEnd = mBuffer + mBufferSize;
	mFront = mBuffer;
	mRear = mBuffer;
	//lastError = eErrorCode::NONE;
}
SerializeBuffer::SerializeBuffer(int bufferSize)
{
	mBuffer = new char[bufferSize + 1];
	mBufferSize = bufferSize;
	mDataSize = 0;
	mBufferEnd = mBuffer + mBufferSize;

	mFront = mBuffer;
	mRear = mBuffer;
	//lastError = eErrorCode::NONE;
}

SerializeBuffer::~SerializeBuffer()
{
	delete[] mBuffer;
}

/// <summary>
/// 대입
/// </summary>
/// <param name="clSrcPacket"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator = (SerializeBuffer& srcPacket)
{
	// TODO 미정.
	// 주체의 크기가 더 작은 경우 손실시킬지 아예 하지 않을지.
	return *this;
}

/// <summary>
/// uchar 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (unsigned char value)
{
	if (mBufferEnd < mRear + sizeof(unsigned char))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(unsigned char*)mRear = value;
		mRear += sizeof(unsigned char);
		mDataSize += sizeof(unsigned char);
	}

	return *this;
}

/// <summary>
/// char 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (char value)
{
	if (mBufferEnd < mRear + sizeof(char))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(char*)mRear = value;
		mRear += sizeof(char);
		mDataSize += sizeof(char);
	}

	return *this;
}

/// <summary>
/// short 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (short value)
{
	if (mBufferEnd < mRear + sizeof(short))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(short*)mRear = value;
		mRear += sizeof(short);
		mDataSize += sizeof(short);
	}

	return *this;
}



/// <summary>
/// int 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (int value)
{
	if (mBufferEnd < mRear + sizeof(int))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(int*)mRear = value;
		mRear += sizeof(int);
		mDataSize += sizeof(int);
	}

	return *this;
}

/// <summary>
/// long 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (long value)
{
	if (mBufferEnd < mRear + sizeof(long))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(long*)mRear = value;
		mRear += sizeof(long);
		mDataSize += sizeof(long);
	}

	return *this;
}

/// <summary>
/// float 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (float value)
{
	if (mBufferEnd < mRear + sizeof(float))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(float*)mRear = value;
		mRear += sizeof(float);
		mDataSize += sizeof(float);
	}

	return *this;
}



/// <summary>
/// double 삽입
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator << (double value)
{
	if (mBufferEnd < mRear + sizeof(double))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(double*)mRear = value;
		mRear += sizeof(double);
		mDataSize += sizeof(double);
	}

	return *this;
}
SerializeBuffer& SerializeBuffer::operator << (unsigned long long value)
{
	if (mBufferEnd < mRear + sizeof(unsigned long long))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(unsigned long long*)mRear = value;
		mRear += sizeof(unsigned long long);
		mDataSize += sizeof(unsigned long long);
	}

	return *this;
}
SerializeBuffer& SerializeBuffer::operator << (DWORD value)
{
	if (mBufferEnd < mRear + sizeof(DWORD))
	{
		//lastError = eErrorCode::FAILED_WRITE;
	}
	else
	{
		*(DWORD*)mRear = value;
		mRear += sizeof(DWORD);
		mDataSize += sizeof(DWORD);
	}

	return *this;
}
/// <summary>
/// BYTE(uchar) 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (BYTE& value)
{
	if (mDataSize < sizeof(BYTE))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(BYTE*)mFront;
		mFront += sizeof(BYTE);
		mDataSize -= sizeof(BYTE);
	}

	return *this;
}

/// <summary>
/// char 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (char& value)
{
	if (mDataSize < sizeof(char))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(char*)mFront;
		mFront += sizeof(char);
		mDataSize -= sizeof(char);
	}

	return *this;
}

/// <summary>
/// short 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (short& value)
{
	if (mDataSize < sizeof(short))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(short*)mFront;
		mFront += sizeof(short);
		mDataSize -= sizeof(short);
	}

	return *this;
}

/// <summary>
/// WORD(ushort) 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (WORD& value)
{
	if (mDataSize < sizeof(WORD))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(WORD*)mFront;
		mFront += sizeof(WORD);
		mDataSize -= sizeof(WORD);
	}

	return *this;
}

/// <summary>
/// int 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (int& value)
{
	if (mDataSize < sizeof(int))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(int*)mFront;
		mFront += sizeof(int);
		mDataSize -= sizeof(int);
	}

	return *this;
}

/// <summary>
/// DWORD(ulong)추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (DWORD& value)
{
	if (mDataSize < sizeof(DWORD))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(DWORD*)mFront;
		mFront += sizeof(DWORD);
		mDataSize -= sizeof(DWORD);
	}

	return *this;
}

/// <summary>
/// float 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (float& value)
{
	if (mDataSize < sizeof(float))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(float*)mFront;
		mFront += sizeof(float);
		mDataSize -= sizeof(float);
	}

	return *this;
}

/// <summary>
/// __int64 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (__int64& value)
{
	if (mDataSize < sizeof(__int64))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(__int64*)mFront;
		mFront += sizeof(__int64);
		mDataSize -= sizeof(__int64);
	}

	return *this;
}

SerializeBuffer& SerializeBuffer::operator >> (unsigned long long& value)
{
	if (mDataSize < sizeof(unsigned long long))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(unsigned long long*)mFront;
		mFront += sizeof(unsigned long long);
		mDataSize -= sizeof(unsigned long long);
	}

	return *this;
}

/// <summary>
/// double 추출
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
SerializeBuffer& SerializeBuffer::operator >> (double& value)
{
	if (mDataSize < sizeof(double))
	{
		//lastError = eErrorCode::FAILED_READ;
	}
	else
	{
		value = *(double*)mFront;
		mFront += sizeof(double);
		mDataSize -= sizeof(double);
	}

	return *this;
}

/// <summary>
/// 데이터를 얻는다.
/// </summary>
/// <param name="dest">데이터를 복사할 포인터</param>
/// <param name="size">크기</param>
/// <returns>얻는데 성공한 크기</returns>
int	SerializeBuffer::GetData(char* dest, int size)
{
	if (size > mDataSize)
		size = mDataSize;

	memcpy(dest, mFront, size);
	mFront += size;
	mDataSize -= size;

	return size;
}





int SerializeBuffer::MoveFront(int size)
{
	if (size > mDataSize)
		size = mDataSize;
	mFront += size;
	mDataSize -= size;

	return size;
}

int SerializeBuffer::MoveRear(int size)
{
	if (mBufferEnd < mRear + size)
	{
		//lastError = eErrorCode::FAILED_WRITE;
		size = 0;
	}
	else
	{
		mRear += size;
		mDataSize += size;
	}

	return size;
}

int SerializeBuffer::Expand()
{
	int expandSize = mBufferSize * 2;
	char* temp = new char[expandSize + 1];
	memcpy(temp, mBuffer, mBufferSize);
	mFront = temp + (mFront - mBuffer);
	mRear = temp + (mRear - mBuffer);
	delete[] mBuffer;
	mBuffer = temp;
	mBufferSize = expandSize;
	mBufferEnd = mBuffer + expandSize;
	return expandSize;
}