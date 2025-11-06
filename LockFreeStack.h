#pragma once
#include <Windows.h>
#include "ObjectPool.h"
#include "Log.h"


//--------------------------------------------
// 락 프리 스택 구현체
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
		size = 0;
		top = nullptr;
	}

	~CLockFreeStack()
	{
		while (1)
		{
			T popValue;
			bool ret = Pop(popValue);
			if (ret == false)
				break;
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
		InterlockedIncrement(&size);
	}
	

	bool Pop(T& value)
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
		InterlockedDecrement(&size);
		
		//-----------------------------------------
		// Pop한 노드의 값 반환 및 노드 풀에 반납
		//-----------------------------------------
		value = maskedT->data;
		nodePool.freeObject(maskedT);
		return true;
	}

	inline ULONG GetSize()
	{
		return size;
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


private:
	Node* top;
	ULONG size;

	//--------------------------------------------
	// Node*의 하위 47비트 추출할 마스크
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;


	CObjectPool_LF<CLockFreeStack<T>::Node> nodePool;
};


