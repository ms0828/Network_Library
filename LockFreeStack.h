#pragma once
#include <Windows.h>
#include "ObjectPool.h"


//--------------------------------------------
// �� ���� ���� ����ü
//--------------------------------------------
template<typename T>
class CLockFreeStack
{
public:
	class Node
	{
	public:
		Node()
		{
			next = nullptr;
		};

	public:
		T data;
		Node* next;
	};


public:
	CLockFreeStack() : nodePool(false)
	{
		stackSize = 0;
		top = nullptr;
	}

	~CLockFreeStack()
	{
		Node* curNode = UnpackingNode(top);
		while (curNode != nullptr)
		{
			Node* deleteNode = curNode;
			curNode = curNode->next;
			delete deleteNode;
		}
	}
	
	void Push(T& data)
	{
		Node* newNode = nodePool.allocObject();
		newNode->data = data;

		Node* t = nullptr;
		Node* nextTop;
		do
		{
			t = top;
			Node* maskedT = UnpackingNode(t);
			newNode->next = maskedT;
			nextTop = PackingNode(newNode, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);
		InterlockedIncrement(&stackSize);
	}
	

	boolean Pop(T& value)
	{
		Node* t;
		Node* nextTop;
		Node* maskedT;
		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			if (maskedT == nullptr)
				return false;

			nextTop = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
		}while(InterlockedCompareExchangePointer((void* volatile *)&top, nextTop, t) != t);
		InterlockedDecrement(&stackSize);
		
		//-----------------------------------------
		// Pop�� ����� �� ��ȯ �� ��� Ǯ�� �ݳ�
		//-----------------------------------------
		value = maskedT->data;
		nodePool.freeObject(maskedT);
		return true;
	}


private:
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


public:
	Node* top;
	ULONGLONG stackSize;

	//--------------------------------------------
	// Node*�� ���� 47��Ʈ ������ ����ũ
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;


	CObjectPool<CLockFreeStack::Node> nodePool;
};


