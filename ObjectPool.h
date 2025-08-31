#pragma once


template<typename T>
struct DefaultCreator
{
	void operator()(T* place) const
	{
		new (place) T();
	}
};


template<typename T, typename Creator = DefaultCreator<T>>
class CObjectPool
{
private:
	struct Node
	{
		T instance;
		unsigned short seed;
		Node* next;
	};

	Node* head;
	Node* tail;
	bool bHasReference;
	unsigned short poolSeed;
	unsigned int poolCnt;


	//----------------------------------------------
	// 오브젝트 생성기(인자 보관 + 생성 호출)
	// - Creator가 정의한 생성자를 호출하게 되며 외부에서 주입되어야한다.
	// [Creator 예시]
	// struct Creator~
	// {
	//		매겨변수 Type arg;
	//		void operator()(T* place) const
	//		{
	//			new (place) T(arg);
	//		}
	// }
	//----------------------------------------------
	Creator creator;

public:

	//----------------------------------------------------
	// 오브젝트 프리 리스트 (Creator 주입 X)
	//----------------------------------------------------
	CObjectPool(bool _bHasReference)
	{
		poolSeed = rand();
		bHasReference = _bHasReference;
		head = (Node*)malloc(sizeof(Node));
		tail = (Node*)malloc(sizeof(Node));
		head->next = tail;
		tail->next = nullptr;
		poolCnt = 0;
	}

	//----------------------------------------------------
	// 오브젝트 프리 리스트 (Creator 주입 O)
	//----------------------------------------------------
	CObjectPool(bool _bHasReference, const Creator& c) : creator(c)
	{
		poolSeed = rand();
		bHasReference = _bHasReference;
		head = (Node*)malloc(sizeof(Node));
		tail = (Node*)malloc(sizeof(Node));
		head->next = tail;
		tail->next = nullptr;
		poolCnt = 0;
	}

	//----------------------------------------------------
	// 오브젝트 풀 (Creator 주입 X)
	//----------------------------------------------------
	CObjectPool(bool _bHasReference, int poolNum)
	{
		poolSeed = rand();
		bHasReference = _bHasReference;
		head = (Node*)malloc(sizeof(Node));
		tail = (Node*)malloc(sizeof(Node));
		head->next = tail;
		tail->next = nullptr;

		for (int i = 0; i < poolNum; i++)
		{
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->next = head->next;
			head->next = newNode;
			newNode->seed = poolSeed;

			// bHasReference가 true인 경우에만 생성자 호출
			if (bHasReference)
			{
				T* instance = (T*)newNode;
				creator(instance);
			}
		}
		poolCnt = poolNum;
	}

	//----------------------------------------------------
	// 오브젝트 풀 (Creator 주입 O)
	//----------------------------------------------------
	CObjectPool(bool _bHasReference, int poolNum, const Creator& c) : creator(c)
	{
		poolSeed = rand();
		bHasReference = _bHasReference;
		head = (Node *)malloc(sizeof(Node));
		tail = (Node *)malloc(sizeof(Node));
		head->next = tail;
		tail->next = nullptr;

		for (int i = 0; i < poolNum; i++)
		{
			Node* newNode = (Node *)malloc(sizeof(Node));
			newNode->next = head->next;
			head->next = newNode;
			newNode->seed = poolSeed;

			// bHasReference가 true인 경우에만 생성자 호출
			if(bHasReference)
			{
				T* instance = (T*)newNode;
				creator(instance);
			}
		}
		poolCnt = poolNum;
	}

	~CObjectPool()
	{
		Node* curNode = head;
		while (curNode != nullptr)
		{
			Node* deleteNode = curNode;
			curNode = curNode->next;

			if (deleteNode == head || deleteNode == tail)
				free(deleteNode);
			else
				delete deleteNode;
		}
	}

	T* allocObject()
	{
		// 풀이 비어있을 때 오브젝트를 새로 생성하여 할당받는다.
		// -> 무조건 생성자 호출
		// 풀에 저장된 오브젝트를 할당 받을 때는 bHasReference가 꺼져 있는 경우만 생성자가 호출
		if (poolCnt == 0)
		{
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->seed = poolSeed;
			creator(&(newNode->instance));
			return &(newNode->instance);
		}
		else
		{
			Node* allocNode = head->next;
			head->next = allocNode->next;
			allocNode->seed = poolSeed;
			if (!bHasReference)
				creator(&(allocNode->instance));
			
			poolCnt--;
			return &(allocNode->instance);
		}
	}

	bool freeObject(T* objectPtr)
	{
		Node* insertNode = (Node*)objectPtr;
		if (insertNode->seed != poolSeed)
			return false;
		insertNode->next = head->next;
		head->next = insertNode;
		if (!bHasReference)
			objectPtr->~T();
		poolCnt++;
		return true;
	}
	
	unsigned int GetPoolCnt()
	{
		return poolCnt;
	}

};