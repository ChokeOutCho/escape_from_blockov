#pragma once
#include "LockFreePool.h"
#include "TLSObjectPool.h"
template <class T>
class LockFreeQueue
{
public:
	LockFreeQueue()
	{
		m_size = 0;
		m_head = m_nodePool.Alloc();
		m_tail = m_head;
		m_head->next = nullptr;

		allocCnt = 0;
		identifier = InterlockedIncrement(&IdentifierPool);
	}

	__forceinline void Enqueue(T t)
	{
		Node* newNode = m_nodePool.Alloc();
		Node* newNodeAdr = newNode;
		newNode->data = t;
		newNode->next = nullptr;
		Node* nextCap;
		Node* nextCapAdr;
		Node* oldTail;
		Node* tailCapAdr;
		Node* tailCap;
		unsigned long tailCnt = GetCountByQueue();
		newNode->next = (Node*)SetCount(0, identifier);
		newNode = (Node*)SetCount(newNode, tailCnt);
		while (1)
		{
			tailCap = m_tail;
			tailCapAdr = (Node*)((unsigned long long)tailCap & ADR_MASK);
			nextCap = tailCapAdr->next;
			nextCapAdr = (Node*)((unsigned long long)nextCap & ADR_MASK);
			
			if (nextCapAdr != 0)
			{
				InterlockedCompareExchangePointer((PVOID*)&m_tail, nextCap, tailCap);
				continue;
			}

			if (InterlockedCompareExchangePointer((PVOID*)&tailCapAdr->next, newNode, nextCap) == nextCap)[[likely]]
			{
				break;
			}

		}
		oldTail = (Node*)InterlockedCompareExchangePointer((PVOID*)&m_tail, newNode, tailCap);
		InterlockedIncrement(&m_size);
	}

	__forceinline int Dequeue(T& t)
	{
		int currentSize = InterlockedDecrement(&m_size);
		if (currentSize < 0)
		{
			InterlockedIncrement(&m_size);
			return -1;
		}

		Node* headCap;
		Node* headCapAdr;
		Node* nextCap;
		Node* nextCapAdr;
		Node* tailCap;
		while (1)
		{
			headCap = m_head;
			tailCap = m_tail;
			headCapAdr = (Node*)((unsigned long long)headCap & ADR_MASK);
			nextCap = headCapAdr->next;
			nextCapAdr = (Node*)((unsigned long long)nextCap & ADR_MASK);

			if (nextCapAdr == nullptr)
			{
				continue;
			}

			if (headCap == tailCap)
			{
				InterlockedCompareExchangePointer((PVOID*)&m_tail, nextCap, tailCap);
				continue;
			}

			t = nextCapAdr->data;
			if (InterlockedCompareExchangePointer((PVOID*)&m_head, nextCap, headCap) == headCap)[[likely]]
			{
				m_nodePool.Free(headCapAdr);
				break;
			}

		}
		return 0;
	}

	__forceinline __int32 GetUseSize()
	{
		return m_size;
	}

	__forceinline void ClearBuffer()
	{
		m_size = 0;
		m_head = m_nodePool.Alloc();
		m_tail = m_head;
		m_head->next = nullptr;

		allocCnt = 0;
	}

private:
	struct Node
	{
		Node* next;
		T data;
	};
	Node* m_tail;
	Node* m_head;
	long m_size;
	//LockFreePool<Node> m_nodePool;
	inline static TLSObjectPool<Node> m_nodePool{512};
	const unsigned long long		ADR_MASK = 0b0000000000000000011111111111111111111111111111111111111111111111;

	__forceinline unsigned long GetCountByQueue()
	{

		return InterlockedIncrement(&allocCnt);
	}

	__forceinline unsigned long GetCount(Node* ptr)
	{
		unsigned long ret = (unsigned long long) ptr >> 47;
		return ret;
	}

	__forceinline unsigned long long SetCount(Node* target, unsigned long long count)
	{
		unsigned long long ret = (unsigned long long)target & ADR_MASK;
		count = count << 47;
		ret = ret | count;
		return ret;
	}

	unsigned long allocCnt;
	long identifier;
	inline static long IdentifierPool;
};