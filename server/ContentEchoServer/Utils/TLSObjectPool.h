#pragma once
#include <new>
#include <stack>
#include "Windows.h"
#include <vector>

template <class T>
class ObjectPool_TLSPoolComponent;

template <class T>
class TLSObjectPool
{
public:
	TLSObjectPool(__int32 chunkSize = 512)
	{
		m_chunkSize = chunkSize;
		InitializeSRWLock(&m_lock_freeSets);
		m_tlsIndex = TlsAlloc();
		if (m_tlsIndex == TLS_OUT_OF_INDEXES)
		{
			//printf("엥");
		}
		m_totalSize = 0;
		m_useSize = 0;
	}
	~TLSObjectPool()
	{
		void* ptr = TlsGetValue(m_tlsIndex);
		if (ptr) {
			delete static_cast<ObjectPool_TLSPoolComponent<T>*>(ptr);
		}
		TlsFree(m_tlsIndex);

	}
	void SetRwnd(__int32 rwnd)
	{
		InterlockedExchange((long*)&m_chunkSize, rwnd);
	}

	T* Alloc()
	{
		LocalSlot* localSlot = GetLocalSlot();
		ObjectPool_TLSPoolComponent<T>* pool = &localSlot->LocalPool;

		// 내 tls풀이 비었는가? -> Y: freeSets 검사, N: 할당
		// freeSets가 비었는가? -> Y: new로 1개 할당, N: freeSets에서 set할당.
		if ((pool->m_top == nullptr))
		{
			ObjectSet set;
			AcquireSRWLockExclusive(&m_lock_freeSets);
			if (!m_freeSets.empty())
			{
				set = m_freeSets.top();
				m_freeSets.pop();
				pool->SetPool(set.TopPtr, set.Size);
			}
			else
			{
				if (++localSlot->UseSize >= m_chunkSize)
				{
					InterlockedAdd((long*)&m_useSize, localSlot->UseSize);
					localSlot->UseSize = 0;
				}
			}
			ReleaseSRWLockExclusive(&m_lock_freeSets);

		}

		return pool->Alloc();

	}

	void Free(T* ptr)
	{
		LocalSlot* localSlot = GetLocalSlot();
		ObjectPool_TLSPoolComponent<T>* pool = &localSlot->LocalPool;

		pool->Free(ptr);

		if (m_chunkSize <= ++pool->m_freeCnt)
		{
			AcquireSRWLockExclusive(&m_lock_freeSets);
			m_freeSets.push({ pool->m_rtop, pool->m_freeCnt });
			ReleaseSRWLockExclusive(&m_lock_freeSets);
			pool->m_rtop = nullptr;
			pool->m_freeCnt = 0;
		}
	}
	__inline __int32 GetUseSize()
	{
		return m_useSize;
	}
	friend class ObjectPool_TLSPoolComponent<T>;
	struct ObjectSet
	{
		void* TopPtr;
		__int32 Size;
	};
	struct LocalSlot
	{
		ObjectPool_TLSPoolComponent<T> LocalPool;
		__int32 UseSize = 0;
	};
private:
	__inline LocalSlot* GetLocalSlot()
	{
		void* ptr = TlsGetValue(m_tlsIndex);
		if (!ptr)
		{
			ptr = new LocalSlot();
			TlsSetValue(m_tlsIndex, ptr);
		}
		return (LocalSlot*)ptr;
	}
	DWORD m_tlsIndex;

	std::stack<ObjectSet> m_freeSets;
	SRWLOCK m_lock_freeSets;

	__int32 m_chunkSize;
	__int32 m_useSize;
	__int32 m_totalSize;
};

template <class T>
class ObjectPool_TLSPoolComponent
{
public:
	T* Alloc()
	{
		Node* temp;

		// 내부 스택이 비어있는 경우에는 노드를 생성한다.
		// 이 경우에는 항상 기본 생성자를 호출한다.
		// 스택을 거치지 않고 반환하기 때문이다.
		if (m_top == nullptr)
		{
			temp = new Node;
		}
		// 내부 스택에 노드가 있는 경우에는 top노드의 포인터(데이터의 포인터)를 pop하여 반환한다.
		// 풀의 생성자 호출 옵션에 따라 생성자 호출을 결정한다.
		else
		{
			temp = m_top;
			m_top = m_top->Next;
			m_currentSize--;

			if (m_callConstructor)
				new (&temp->Data) T();
		}

		// IsStacking을 0으로 설정하여 스택과의 연결이 끊겼음을 명시한다.
		// 이 변수는 Free()에서 중복 해제를 감지하기 위해 사용된다.

		return (T*)temp;
	}

	void Free(T* ptr)
	{
		Node* node = (Node*)ptr;

		if (m_callConstructor)
			node->Data.~T();

		node->Next = m_rtop;
		m_rtop = node;
	}

	ObjectPool_TLSPoolComponent()
	{
		m_top = nullptr;
		m_rtop = nullptr;
		m_currentSize = 0;
		m_callConstructor = false;
		m_freeCnt = 0;
	}
	ObjectPool_TLSPoolComponent(int size, bool callConstructor = false)
	{
		m_top = nullptr;
		m_rtop = nullptr;
		m_currentSize = 0;
		m_callConstructor = callConstructor;
		m_freeCnt = 0;

		for (int i = 0; i < size; i++)
		{
			CreateNode();
		}
	}
	~ObjectPool_TLSPoolComponent()
	{

	}

	__inline void SetPool(void* top, __int32 size)
	{
		m_top = (Node*)top;
		m_currentSize = size;
	}

	friend class TLSObjectPool<T>;

private:
#pragma warning(push)
#pragma warning(disable : 26495) // C26495 멤버 변수 초기화 경고문 억제
	struct Node
	{
		T Data;
		Node* Next;
	};
#pragma warning(pop)


#pragma warning(push)
#pragma warning(disable : 6011) // C6011 malloc() 실패로 인한 null참조 경고문 억제
	void CreateNode()
	{
		Node* temp;
		// 생성자 호출 옵션에 따라 최초 생성시 기본 생성자 호출을 결정한다.
		if (m_callConstructor == false)
			temp = new Node;
		else
			temp = (Node*)malloc(sizeof(Node));

		// 스택에 삽입하는 동작
		temp->Next = m_top;
		m_top = temp;
		m_currentSize++;
	}
#pragma warning(pop)

	// 스택의 탑은 객체 공간 내에서 위치가 변경되어서는 안된다.
	Node* m_top;
	Node* m_rtop;

	__int32 m_currentSize;
	bool	m_callConstructor;
	__int32 m_freeCnt;
};
