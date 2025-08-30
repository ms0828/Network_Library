#pragma once
#include <iostream>

#define MAX_POOLSIZE 128

template<typename T>
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

public:

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

	CObjectPool(bool _bHasReference, int poolNum)
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
				new (instance) T();
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
			new (newNode) T();
			return &(newNode->instance);
		}
		else
		{
			Node* allocNode = head->next;
			head->next = allocNode->next;
			allocNode->seed = poolSeed;
			if (!bHasReference)
				new (allocNode) T();
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
	
};