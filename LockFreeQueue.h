#pragma once
#include <Windows.h>
#include "ObjectPool.h"


//-----------------------------------------------------
// 락 프리 큐 구현부
//-----------------------------------------------------
template<typename T>
class CLockFreeQueue
{
public:
	class Node
	{
	public:
		Node()
		{
			next = nullptr;
		}
	public:
		T data;
		Node* next;
	};

public:
	CLockFreeQueue() : nodePool(false)
	{
		size = 0;
		head = nodePool.allocObject();
		tail = head;
	}
	
	~CLockFreeQueue()
	{
		while (1)
		{
			T popValue;
			bool ret = Dequeue(popValue);
			if (ret == false)
				break;
		}
	}

	void Enqueue(T data)
	{
		Node* newNode = nodePool.allocObject();
		newNode->data = data;

		while (1)
		{
			Node* t = tail;
			Node* maskedT = UnpackingNode(t);
			Node* nextTail = PackingNode(newNode, GetNodeStamp(t) + 1);	

			//--------------------------------------------------------------------------------
			// 1번 CAS
			//  - 바라본 tail의 next가 null이라면 할당 받은 노드를 next에 연결시킨다.
			// 2번 CAS
			//  - tail의 변화가 없다면 tail을 할당 받은 노드로 이동시킨다.
			// 
			// [ 1번 CAS가 실패하는 경우 ]
			//  - 다른 스레드에서 먼저 바라본 tail에 노드를 연결시킨 경우
			//  - 혹은 tail이 아직 덜 이동한 상태에서 Enqueue를 시도하는 경우
			//  => tail의 변화가 없다면 tail 이동을 시도한다.
			// 
			// [ 2번 CAS가 실패하는 경우 ]
			//  - ABA로 인해서 1번 CAS만 통과하고 2번 CAS는 실패한 경우
			//  - 다른 스레드의 Enqueue 및 Dequeue에서 tail을 대신 이동한 경우
			//  => 다른 스레드에서의 tail 이동을 기대하며 2번 CAS가 실패하는 경우를 허용하는 구조
			//--------------------------------------------------------------------------------
			if (InterlockedCompareExchangePointer((PVOID*)&maskedT->next, newNode, nullptr) == nullptr)
			{
				InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t);
				break;
			}
			else
			{
				Node* nextTail = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
				InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t);
			}
		}
		InterlockedIncrement(&size);
	}

	
	bool Dequeue(T& value)
	{
		while (1)
		{
			Node* h = head;
			Node* maskedH = UnpackingNode(h);
			Node* next = maskedH->next;
			Node* t = tail;
			Node* maskedT = UnpackingNode(t);

			//--------------------------------------------------------------------------------
			// 현재 큐가 비었는지 판단
			// - if(head == tail)
			//--------------------------------------------------------------------------------
			if (maskedH == maskedT)
			{
				//--------------------------------------------------------------------------------
				// h가 head랑 같지 않다면 이는 ABA에 의한 조건 통과이다.
				// - head를 캐싱하고 컨텍스트 스위칭 되었다가 나중에 같은 노드로 tail을 갱신하는 경우
				//--------------------------------------------------------------------------------
				if (h != head)
					continue;

				//--------------------------------------------------------------------------------
				// 1. next == null
				//	- 진짜 큐가 비어있는 상태 
				// 2. next != null
				//  - 노드는 존재하지만 tail이 아직 이동하지 않은 상태
				//  - tail 이동 시도 및 Dequeue 재시도
				//--------------------------------------------------------------------------------
				if (next == nullptr)
					return false;
				
				Node* nextTail = PackingNode(next, GetNodeStamp(t) + 1);
				InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t);
				continue;
			}

			//-----------------------------------------------------------------------------------
			// [ ABA에 의한 예외 처리 ]
			// CAS 전에 Context Switching 일어났고, 깨어난 시점에 maskedH가 재사용되면 next는 null이 된다.
			// - 재사용에 의한 next null은 다시 시도한다.
			//-----------------------------------------------------------------------------------
			if (next == nullptr && h != head)
				continue;
			
			//-----------------------------------------------------------------------------------
			// Dequeue 시도
			// - 누군가 head를 변경하였다면 Dequeue 재시도
			//-----------------------------------------------------------------------------------
			T retValue = next->data;
			Node* nextHead = PackingNode(next, GetNodeStamp(h) + 1);
			if (InterlockedCompareExchangePointer((PVOID*)&head, nextHead, h) == h)
			{
				value = retValue;
				nodePool.freeObject(maskedH);
				break;
			}
		}
		
		InterlockedDecrement(&size);
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
	Node* head;
	Node* tail;
	ULONGLONG size;

	//--------------------------------------------
	// Node*의 하위 47비트 추출할 마스크
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;

	CObjectPool_LF<CLockFreeQueue::Node, true> nodePool;

};