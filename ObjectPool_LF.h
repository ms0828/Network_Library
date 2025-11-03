#pragma once
#include "Log.h"

#define PROFILE
#include "Profiler.h"

#define dfFenceValue 0xdddddddddddddddd

#define dfDebugObjectPool

template<typename T>
class CObjectPool_LF
{
private:
	struct Node
	{
		ULONGLONG headFence;
		T instance;
		ULONGLONG tailFence;
		USHORT seed;
		Node* next;
	};

public:

	template<typename... Args>
	CObjectPool_LF(bool preConstructor, int poolNum = 0, Args&&... args)
	{
		poolSeed = rand();
		bPreConstructor = preConstructor;
		top = nullptr;
		allocCnt = 0;
		poolCnt = 0;

		for (int i = 0; i < poolNum; i++)
		{
			Node* newNode = (Node*)malloc(sizeof(Node));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = top;
			top = PackingNode(newNode, GetNodeStamp(top) + 1);

			//--------------------------------------------------------
			// bPreConstructor가 true인 경우, 처음 생성 시 생성자 호출
			//--------------------------------------------------------
			if (bPreConstructor)
			{
				T* instance = &newNode->instance;
				new (instance) T(std::forward<Args>(args)...);
			}

			poolCnt++;
		}
	}

	~CObjectPool_LF()
	{
		while (1)
		{
			Node* t;
			Node* maskedT;
			Node* nextTop;
			do
			{
				t = top;
				maskedT = UnpackingNode(t);
				if (maskedT == nullptr)
					return;
				nextTop = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
			} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);
			//------------------------------------------------------------------------------
			// bPreConstructor가 true인 경우, ObjectPool이 소멸될 때 instance의 소멸자 호출
			//------------------------------------------------------------------------------
			if (bPreConstructor)
				delete maskedT;
			else
				free(maskedT);
		}
	}


	//---------------------------------------------------------------
	// 할당 정책
	// - bPreConstructor가 true인 경우, allocObject마다 생성자 호출 X
	// - bPreConstructor가 false인 경우, allocObject마다 생성자 호출 O
	// 
	// 풀이 비어있을 때는 instance를 새로 생성하여 할당
	// - 생성자 호출 O
	//---------------------------------------------------------------
	template<typename... Args>
	T* allocObject(Args&&... args)
	{
		PRO_BEGIN("Alloc");

		Node* t = nullptr;
		Node* nextTop = nullptr;
		Node* maskedT = nullptr;

		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			if (maskedT == nullptr)
			{
				Node* newNode = (Node*)malloc(sizeof(Node));
				newNode->headFence = dfFenceValue;
				newNode->tailFence = dfFenceValue;
				newNode->seed = poolSeed;
				newNode->next = nullptr;
				T* instance = &newNode->instance;
				new (instance) T(std::forward<Args>(args)...);
#ifdef dfDebugObjectPool
				InterlockedIncrement(&allocCnt);
#endif
				return instance;
			}

			nextTop = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);

		T* instance = &maskedT->instance;
		if (!bPreConstructor)
			new (instance) T(std::forward<Args>(args)...);

#ifdef dfDebugObjectPool
		InterlockedDecrement(&poolCnt);
		InterlockedIncrement(&allocCnt);
#endif

		PRO_END("Alloc");

		return instance;
	}

	//---------------------------------------------------------------
	// 반납 정책
	// - bPreConstructor가 true인 경우, freeObject마다 소멸자 호출 X
	// - bPreConstructor가 false인 경우, freeObject마다 소멸자 호출 O
	// 
	// 
	// 안전 장치 [ dfDebugObjectPool가 정의되어 있을 경우 반납 시 노드 검증 수행 ]
	// - Node의 seed가 Pool의 seed와 동일한지 확인
	// - Node안의 instance 앞 뒤로 fence를 두어 값이 오염되어있는지 확인
	//---------------------------------------------------------------
	bool freeObject(T* objectPtr)
	{
		PRO_BEGIN("Free");

		Node* freeNode;
		int t1 = alignof(T);
		int t2 = alignof(ULONGLONG);
		if (alignof(T) > alignof(ULONGLONG))
		{
			int remainAlign = alignof(T) - alignof(ULONGLONG);
			freeNode = (Node*)((char*)objectPtr - remainAlign - sizeof(ULONGLONG));
		}
		else
			freeNode = (Node*)((char*)objectPtr - sizeof(ULONGLONG));

#ifdef dfDebugObjectPool
		if (freeNode->seed != poolSeed)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"[ObjectPool Error] : Miss match poolSeed / freeObject Node : %016llx / Seed(%hu) != poolSeed(%hu)\n", freeNode, freeNode->seed, poolSeed);
			__debugbreak();
			return false;
		}
		if (freeNode->headFence != dfFenceValue || freeNode->tailFence != dfFenceValue)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"[ObjectPool Error] : memory access overflow (fence is not dfFenceValue) / freeObject Node : %016llx / Seed = %hu ", freeNode, freeNode->seed);
			__debugbreak();
			return false;
		}
#endif

		Node* t;
		Node* nextTop;
		Node* maskedT;
		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			freeNode->next = maskedT;
			nextTop = PackingNode(freeNode, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);


		//------------------------------------------------------------------------------------------------
		// 락 프리 스택을 이용한 오브젝트 풀 구현의 한계
		// - 락을 걸지 않았으니 소멸자 호출 시점에 이미 다른 스레드에서 해당 오브젝트를 할당 받았을 수 있다.
		// - 락 프리 스택 오브젝트 풀은 반납 시 소멸자 호출 불가 (어차피 락 프리 스택 오브젝트 풀은 락 프리 자료구조의 노드 풀에서만 사용하기 때문에 소멸자 호출이 필요 없음)
		//------------------------------------------------------------------------------------------------
		//if (!bPreConstructor)
			//objectPtr->~T();

#ifdef dfDebugObjectPool
		InterlockedIncrement(&poolCnt);
		InterlockedDecrement(&allocCnt);
#endif

		PRO_END("Free");

		return true;
	}

	inline ULONG GetPoolCnt()
	{
		return poolCnt;
	}

	inline ULONG GetAllocCnt()
	{
		return allocCnt;
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
	ULONG allocCnt;
	ULONG poolCnt;


	//--------------------------------------------
	// Node*의 하위 47비트 추출할 마스크
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;
};

