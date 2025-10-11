#pragma once
#include <Windows.h>
#include "ObjectPool.h"


//-----------------------------------------------------
// �� ���� ť ������
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
			// 1�� CAS
			//  - �ٶ� tail�� next�� null�̶�� �Ҵ� ���� ��带 next�� �����Ų��.
			// 2�� CAS
			//  - tail�� ��ȭ�� ���ٸ� tail�� �Ҵ� ���� ���� �̵���Ų��.
			// 
			// [ 1�� CAS�� �����ϴ� ��� ]
			//  - �ٸ� �����忡�� ���� �ٶ� tail�� ��带 �����Ų ���
			//  - Ȥ�� tail�� ���� �� �̵��� ���¿��� Enqueue�� �õ��ϴ� ���
			//  => tail�� ��ȭ�� ���ٸ� tail �̵��� �õ��Ѵ�.
			// 
			// [ 2�� CAS�� �����ϴ� ��� ]
			//  - ABA�� ���ؼ� 1�� CAS�� ����ϰ� 2�� CAS�� ������ ���
			//  - �ٸ� �������� Enqueue �� Dequeue���� tail�� ��� �̵��� ���
			//  => �ٸ� �����忡���� tail �̵��� ����ϸ� 2�� CAS�� �����ϴ� ��츦 ����ϴ� ����
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
			// ���� ť�� ������� �Ǵ�
			// - if(head == tail)
			//--------------------------------------------------------------------------------
			if (maskedH == maskedT)
			{
				//--------------------------------------------------------------------------------
				// h�� head�� ���� �ʴٸ� �̴� ABA�� ���� ���� ����̴�.
				// - head�� ĳ���ϰ� ���ؽ�Ʈ ����Ī �Ǿ��ٰ� ���߿� ���� ���� tail�� �����ϴ� ���
				//--------------------------------------------------------------------------------
				if (h != head)
					continue;

				//--------------------------------------------------------------------------------
				// 1. next == null
				//	- ��¥ ť�� ����ִ� ���� 
				// 2. next != null
				//  - ���� ���������� tail�� ���� �̵����� ���� ����
				//  - tail �̵� �õ� �� Dequeue ��õ�
				//--------------------------------------------------------------------------------
				if (next == nullptr)
					return false;
				
				Node* nextTail = PackingNode(next, GetNodeStamp(t) + 1);
				InterlockedCompareExchangePointer((PVOID*)&tail, nextTail, t);
				continue;
			}

			//-----------------------------------------------------------------------------------
			// [ ABA�� ���� ���� ó�� ]
			// CAS ���� Context Switching �Ͼ��, ��� ������ maskedH�� ����Ǹ� next�� null�� �ȴ�.
			// - ���뿡 ���� next null�� �ٽ� �õ��Ѵ�.
			//-----------------------------------------------------------------------------------
			if (next == nullptr && h != head)
				continue;
			
			//-----------------------------------------------------------------------------------
			// Dequeue �õ�
			// - ������ head�� �����Ͽ��ٸ� Dequeue ��õ�
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
	// Node*�� ���� 47��Ʈ ������ ����ũ
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;

	CObjectPool<CLockFreeQueue::Node> nodePool;

};