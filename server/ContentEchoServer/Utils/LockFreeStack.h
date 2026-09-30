#pragma once
#include "TLSObjectPool.h"
template <class T>
class LockFreeStack
{
	struct Node
	{
		Node* Next;
		T Data;
	};

public:
	bool Push(const T data)
	{
		Node* newTop = m_nodePool.Alloc();
		Node* newTopAdr = newTop;
		newTop->Data = data;
		Node* oldTop;
		unsigned long allocCnt = GetCountByStack();
		newTop = (Node*)SetCount(newTop, allocCnt);

		do
		{
			oldTop = m_top;
			newTopAdr->Next = oldTop;

		} while (InterlockedCompareExchange64((__int64*)&m_top, (__int64)newTop, (__int64)oldTop) != (__int64)oldTop);
		InterlockedIncrement(&m_size);
		return true;
	}

	bool Pop(T* outData)
	{
		Node* oldTop;
		Node* newTop;
		Node* oldTopAdr;
		do
		{
			oldTop = m_top;
			oldTopAdr = (Node*)((unsigned long long)oldTop & ADR_MASK);
			if (oldTopAdr == nullptr)
			{
				return false;
			}
			else
			{
				newTop = oldTopAdr->Next;
			}


		} while ((Node*)InterlockedCompareExchange64((__int64*)&m_top, (__int64)newTop, (__int64)oldTop) != oldTop);
		InterlockedDecrement(&m_size);
		*outData = oldTopAdr->Data;
		m_nodePool.Free(oldTopAdr);
		return true;
	}

	bool Push(const T data, long* outRaceCount)
	{
		long raceCount = 0;
		Node* newTop = m_nodePool.Alloc();
		Node* newTopAdr = newTop;
		newTop->Data = data;
		Node* oldTop;
		unsigned long allocCnt = GetCountByStack();
		newTop = (Node*)SetCount(newTop, allocCnt);
		do
		{
			raceCount++;
			oldTop = m_top;
			newTopAdr->Next = oldTop;

		} while (InterlockedCompareExchange64((__int64*)&m_top, (__int64)newTop, (__int64)oldTop) != (__int64)oldTop);
		InterlockedIncrement(&m_size);
		*outRaceCount = raceCount - 1;
		return true;
	}

	bool Pop(T* outData, long* outRaceCount)
	{
		long raceCount = 0;

		Node* oldTop;
		Node* newTop;
		Node* oldTopAdr;
		T data;
		do
		{
			raceCount++;

			oldTop = m_top;
			oldTopAdr = (Node*)((unsigned long long)oldTop & ADR_MASK);
			if (oldTopAdr == nullptr)
			{
				return false;
			}
			else
			{
				newTop = oldTopAdr->Next;
			}


		} while ((Node*)InterlockedCompareExchange64((__int64*)&m_top, (__int64)newTop, (__int64)oldTop) != oldTop);
		InterlockedDecrement(&m_size);
		*outData = oldTopAdr->Data;
		*outRaceCount = raceCount - 1;
		m_nodePool.Free(oldTopAdr);
		return true;
	}

public:
	LockFreeStack()
	{
		m_size = 0;
		m_top = 0;
		m_allocCnt = 0;
	}
private:
	Node* m_top;
	long m_size;
	inline static TLSObjectPool<Node> m_nodePool{128};
	unsigned long m_allocCnt;

	__inline unsigned long GetCountByStack()
	{

		return InterlockedIncrement(&m_allocCnt);
	}

	__inline unsigned long GetCount(Node* ptr)
	{
		unsigned long ret = (unsigned long long) ptr >> 47;
		return ret;
	}

	__inline unsigned long long SetCount(Node* target, unsigned long long count)
	{
		unsigned long long ret = (unsigned long long)target & ADR_MASK;
		count = count << 47;
		ret = ret | count;
		return ret;
	}
};