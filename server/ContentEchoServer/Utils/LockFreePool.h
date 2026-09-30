#pragma once
#include <new>
#include <windows.h>

const unsigned long long		ADR_MASK = 0b00000000'00000000'01111111'11111111'11111111'11111111'11111111'11111111;

template <class T>
class LockFreePool
{
public:
	T* Alloc()
	{
		Node* oldTop;
		Node* nextTop;
		Node* ret;

		unsigned long cnt = InterlockedIncrement(&m_allocCnt);

		do {
			oldTop = m_top;
			ret = (Node*)((unsigned long long)oldTop & ADR_MASK);

			if (ret == nullptr) {
				ret = new Node;
				ret->PoolID = m_poolID;
				ret->IsStacking = false;
				InterlockedIncrement(&m_maxSize);
				return (T*)ret;
			}
			else {
				nextTop = ret->Next;
				nextTop = (Node*)SetCount(nextTop, cnt);
			}
		} while (InterlockedCompareExchange64(
			(__int64*)&m_top,
			(__int64)nextTop,
			(__int64)oldTop) != (__int64)oldTop);

		InterlockedDecrement(&m_currentStackSize);
		ret->IsStacking = false;
		if (m_callConstructor)
			new (&ret->Data) T();

		return (T*)ret;
	}


	bool Free(T* ptr)
	{
		Node* oldTop;
		Node* node = (Node*)ptr;

		if (node->PoolID != m_poolID || node->IsStacking != false) {
			return false;
		}

		node->IsStacking = true;

		do {
			oldTop = m_top;
			node->Next = oldTop;
		} while (InterlockedCompareExchange64(
			(__int64*)&m_top,
			(__int64)node,
			(__int64)oldTop) != (__int64)oldTop);

		// Free에서는 SetCount 호출하지 않음
		InterlockedIncrement(&m_currentStackSize);

		if (m_callConstructor)
			node->Data.~T();

		return true;
	}


	LockFreePool()
	{
		m_top = nullptr;
		m_maxSize = 0;
		m_currentStackSize = 0;
		m_poolID = rand();
		m_callConstructor = false;
	}
	LockFreePool(int size, bool callConstructor = false)
	{
		m_top = nullptr;
		m_maxSize = 0;
		m_currentStackSize = 0;
		m_poolID = rand();
		m_callConstructor = callConstructor;

		for (int i = 0; i < size; i++)
		{
			CreateNode();
		}
	}
	~LockFreePool()
	{

	}


private:
#pragma warning(push)
#pragma warning(disable : 26495) // C26495 멤버 변수 초기화 경고문 억제
	struct Node
	{
		T Data;
		Node* Next;
		bool IsStacking;
		__int32 PoolID;
	};
#pragma warning(pop)


#pragma warning(push)
#pragma warning(disable : 6011) // C6011 malloc() 실패로 인한 null참조 경고문 억제
	__inline void CreateNode()
	{
		// Push
		Node* temp;
		// 생성자 호출 옵션에 따라 최초 생성시 기본 생성자 호출을 결정한다.
		if (m_callConstructor == false)
			temp = new Node;
		else
			temp = (Node*)malloc(sizeof(Node));

		temp->PoolID = m_poolID;
		m_maxSize++;

		// 스택에 삽입하는 동작
		Node* top = m_top;
		temp->IsStacking = true;
		temp->Next = top;

		unsigned long topCnt = GetCount(top);
		topCnt++;
		temp = (Node*)SetCount(temp, topCnt);

		m_top = temp;
		m_currentStackSize++;
	}
#pragma warning(pop)

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

	Node* m_top;
	unsigned long m_allocCnt;
	long m_poolID;
	long m_maxSize;
	long m_currentStackSize;
	bool m_callConstructor;
};