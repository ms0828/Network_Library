#pragma once
#include <iostream>
#include <Windows.h>
#include "Log.h"


template<typename T>
class CObjectPool
{
public:
	struct Node
	{
	public:
		T instance;
		USHORT seed;
		Node* next;
	};

public:

	//------------------------------------------------------------
	// ������Ʈ ��������Ʈ
	//------------------------------------------------------------
	CObjectPool(bool preConstructor)
	{
		poolSeed = rand();
		bPreConstructor = preConstructor;
		top = nullptr;
		poolCnt = 0;
	}

	//------------------------------------------------------------
	// ������Ʈ Ǯ
	// - ��Ƽ ������ ȯ�濡���� �� ������ ȣ���� ������ ����� ��
	//------------------------------------------------------------
	CObjectPool(bool preConstructor, int poolNum)
	{
		poolSeed = rand();
		bPreConstructor = preConstructor;
		top = nullptr;
		for (int i = 0; i < poolNum; i++)
		{
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->seed = poolSeed;
			newNode->next = top;
			top = newNode;

			// bPreConstructor�� true�� ��쿡�� ������ ȣ��
			if (bPreConstructor)
			{
				T* instance = (T*)newNode;
				new (instance) T();
			}
		}
		poolCnt = poolNum;
	}

	~CObjectPool()
	{
		Node* curNode = UnpackingNode(top);
		while (curNode != nullptr)
		{
			Node* deleteNode = curNode;
			curNode = curNode->next;
			if (bPreConstructor)
				delete deleteNode;
			else
				free(deleteNode);
		}
	}

	T* allocObject()
	{
		Node* t;
		Node* nextTop;
		Node* maskedT;

		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			//----------------------------------------
			// Ǯ�� ������� �� ������Ʈ�� ���� �����Ͽ� �Ҵ�
			//----------------------------------------
			if (maskedT == nullptr)
			{
				Node* newNode = (Node*)malloc(sizeof(Node));
				newNode->seed = poolSeed;
				newNode->next = nullptr;
				new (newNode) T();
				return &(newNode->instance);
			}
			
			nextTop = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);
		InterlockedDecrement(&poolCnt);

		//----------------------------------------
		// bPreConstructor�� ���� �ִ� ��� �Ҵ縶�� �����ڰ� ȣ��
		//----------------------------------------
		if (!bPreConstructor)
			new (maskedT) T();

		return &(maskedT->instance);
	}

	bool freeObject(T* objectPtr)
	{
		Node* freeNode = (Node*)objectPtr;
		if (freeNode->seed != poolSeed)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"Miss match poolSeed / freeObject Node : %016llx / Seed(%hu) != poolSeed(%hu)\n", freeNode, freeNode->seed, poolSeed);
			return false;
		}

		Node* t;
		Node* nextTop;
		do
		{
			t = top;
			Node* maskedT = UnpackingNode(t);
			freeNode->next = maskedT;
			nextTop = PackingNode(freeNode, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);
		InterlockedIncrement(&poolCnt);

		if (!bPreConstructor)
			objectPtr->~T();

		return true;
	}

	ULONG GetPoolCnt()
	{
		return poolCnt;
	}


	inline Node* PackingNode(Node* ptr, ULONGLONG stamp)
	{
		return (Node*)((ULONGLONG)ptr | (stamp << stampShift));
	}
	inline Node* UnpackingNode(Node* ptr)
	{
		return (Node*)((ULONGLONG)ptr & nodeMask);
	}
	inline ULONGLONG GetNodeStamp(Node* ptr)
	{
		return (ULONGLONG)ptr >> stampShift;
	}

private:
	Node* top;
	bool bPreConstructor;
	USHORT poolSeed;
	ULONG poolCnt;


	//--------------------------------------------
	// Node*�� ���� 47��Ʈ ������ ����ũ
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;
};