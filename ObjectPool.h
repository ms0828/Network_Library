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
	// ������Ʈ ������(���� ���� + ���� ȣ��)
	// - Creator�� ������ �����ڸ� ȣ���ϰ� �Ǹ� �ܺο��� ���ԵǾ���Ѵ�.
	// [Creator ����]
	// struct Creator~
	// {
	//		�Űܺ��� Type arg;
	//		void operator()(T* place) const
	//		{
	//			new (place) T(arg);
	//		}
	// }
	//----------------------------------------------
	Creator creator;

public:

	//----------------------------------------------------
	// ������Ʈ ���� ����Ʈ (Creator ���� X)
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
	// ������Ʈ ���� ����Ʈ (Creator ���� O)
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
	// ������Ʈ Ǯ (Creator ���� X)
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

			// bHasReference�� true�� ��쿡�� ������ ȣ��
			if (bHasReference)
			{
				T* instance = (T*)newNode;
				creator(instance);
			}
		}
		poolCnt = poolNum;
	}

	//----------------------------------------------------
	// ������Ʈ Ǯ (Creator ���� O)
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

			// bHasReference�� true�� ��쿡�� ������ ȣ��
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
		// Ǯ�� ������� �� ������Ʈ�� ���� �����Ͽ� �Ҵ�޴´�.
		// -> ������ ������ ȣ��
		// Ǯ�� ����� ������Ʈ�� �Ҵ� ���� ���� bHasReference�� ���� �ִ� ��츸 �����ڰ� ȣ��
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