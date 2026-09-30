#pragma once
#include <new>
#include "Windows.h"
template <class T>
class ObjectPool
{
public:
	T* Alloc()
	{
		Node* temp;

		// 내부 스택이 비어있는 경우에는 노드를 생성한다.
		// 이 경우에는 항상 기본 생성자를 호출한다.
		// 스택을 거치지 않고 반환하기 때문이다.
		if (mTop == nullptr)
		{
			temp = new Node;

			temp->Link = mHead;
			mHead = temp;
			temp->PoolID = mPoolID;
			mMaxSize++;
		}
		// 내부 스택에 노드가 있는 경우에는 top노드의 포인터(데이터의 포인터)를 pop하여 반환한다.
		// 풀의 생성자 호출 옵션에 따라 생성자 호출을 결정한다.
		else
		{
			temp = mTop;
			mTop = mTop->Next;
			mCurrentSize--;

			if (mCallConstructor)
				new (&temp->Data) T();
		}

		// IsStacking을 0으로 설정하여 스택과의 연결이 끊겼음을 명시한다.
		// 이 변수는 Free()에서 중복 해제를 감지하기 위해 사용된다.
		temp->IsStacking = false;

		return (T*)temp;
	}

	void Free(T* ptr)
	{
		Node* node = (Node*)ptr;

		if (node->PoolID != mPoolID || node->IsStacking != false)
		{
			// 이 풀에서 할당하지 않은 객체 해제 감지
			// 중복 해제 시도 감지
			return;
		}

		if (mCallConstructor)
			node->Data.~T();

		node->IsStacking = true;
		node->Next = mTop;
		mTop = node;
		mCurrentSize++;
	}

	__inline int GetPoolMaxSize()
	{
		return mMaxSize;
	}

	__inline int GetPoolCurrentSize()
	{
		return mCurrentSize;
	}

	ObjectPool()
	{
		InitializeCriticalSection(&lock);

		mTop = nullptr;
		mHead = nullptr;
		mMaxSize = 0;
		mCurrentSize = 0;
		mPoolID = rand();
		mCallConstructor = false;
	}
	ObjectPool(int size, bool callConstructor = false)
	{
		InitializeCriticalSection(&lock);
		mTop = nullptr;
		mHead = nullptr;
		mMaxSize = 0;
		mCurrentSize = 0;
		mPoolID = rand();
		mCallConstructor = callConstructor;
		for (int i = 0; i < size; i++)
		{
			CreateNode();
		}
	}
	~ObjectPool()
	{
		while (mHead != nullptr)
		{
			Node* temp = mHead->Link;

			if (mCallConstructor)
			{
				if (!mHead->IsStacking)
					delete mHead;
			}
			else
			{
				delete mHead;
			}
			mHead = temp;
		}
	}

	CRITICAL_SECTION lock;

private:
#pragma warning(push)
#pragma warning(disable : 26495) // C26495 멤버 변수 초기화 경고문 억제
	struct Node
	{
		T Data;
		Node* Next;
		Node* Link;
		bool IsStacking;
		__int32 PoolID;
	};
#pragma warning(pop)


#pragma warning(push)
#pragma warning(disable : 6011) // C6011 malloc() 실패로 인한 null참조 경고문 억제
	void CreateNode()
	{
		Node* temp;
		// 생성자 호출 옵션에 따라 최초 생성시 기본 생성자 호출을 결정한다.
		if (mCallConstructor == false)
			temp = new Node;
		else
			temp = (Node*)malloc(sizeof(Node));

		// 리스트에 삽입하는 동작
		temp->Link = mHead;
		mHead = temp;
		temp->PoolID = mPoolID;
		mMaxSize++;

		// 스택에 삽입하는 동작
		temp->IsStacking = true;
		temp->Next = mTop;
		mTop = temp;
		mCurrentSize++;
	}
#pragma warning(pop)

	// 스택의 탑은 객체 공간 내에서 위치가 변경되어서는 안된다.
	Node* mTop;
	// 리스트의 헤드
	Node* mHead;
	__int32 mPoolID;
	__int32 mMaxSize;
	__int32 mCurrentSize;
	bool mCallConstructor;
};