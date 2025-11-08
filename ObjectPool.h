#pragma once

#define PROFILE
#define dfFenceValue 0xdddddddddddddddd
#define dfNumOfChunkObject 500

#include <Windows.h>
#include <new>
#include <utility>
#include <functional>

#include "Log.h"
#include "Profiler.h"


template<typename T, bool DebugMode>
class CObjectPool_ST;

template<typename T, bool DebugMode>
class CObjectPool_Lock;

template<typename T, bool DebugMode>
class CObjectPool_LF;

template<typename T, bool DebugMode>
class CObjectPool_TLS;


template<typename T>
struct Node
{
	template<typename, bool> friend class CObjectPool_ST;
	template<typename, bool> friend class CObjectPool_Lock;
	template<typename, bool> friend class CObjectPool_LF;
	template<typename, bool> friend class CObjectPool_TLS;
private:
	ULONGLONG headFence;
	T instance;
	ULONGLONG tailFence;
	USHORT seed;
	Node* next;
};



template<typename T, bool DebugMode = false>
class CObjectPool_ST
{
	template<typename, bool> friend class CObjectPool_Lock;
	template<typename, bool> friend class CObjectPool_LF;
	template<typename, bool> friend class CObjectPool_TLS;

public:
	template<typename... Args>
	CObjectPool_ST(bool preConstructor = true, int poolNum = 0, USHORT seed = 0, Args&&... args)
	{
		poolSeed = seed;
		bPreConstructor = preConstructor;
		top = nullptr;
		allocCnt = 0;
		poolCnt = 0;
		creatorFunc = [args...](void* instance)-> T* {
			return new(instance) T(args...);
		};
		
		for (int i = 0; i < poolNum; i++)
		{
			Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = top;
			top = newNode;
			if (bPreConstructor)
				creatorFunc(&newNode->instance);
		}
		poolCnt = poolNum;
		return;
	}

	~CObjectPool_ST()
	{
		while (top != nullptr)
		{
			Node<T>* deleteNode = top;
			top = top->next;
			delete deleteNode;
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
	T* allocObject()
	{
		if (top == nullptr)
		{
			Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = nullptr;

			T* instance = &newNode->instance;
			creatorFunc(instance);
			allocCnt++;
			return instance;
		}
		Node<T>* allocNode = top;
		top = allocNode->next;
		T* instance = &allocNode->instance;
		if (!bPreConstructor)
			creatorFunc(instance);
		poolCnt--;
		allocCnt++;
		return instance;
	}

	//---------------------------------------------------------------
	// 반납 정책
	// - bPreConstructor가 true인 경우, freeObject마다 소멸자 호출 X
	// - bPreConstructor가 false인 경우, freeObject마다 소멸자 호출 O
	// 
	// 안전 장치
	// - Node의 seed가 Pool의 seed와 동일한지 확인
	// - Node안의 instance 앞 뒤로 fence를 두어 값이 오염되어있는지 확인
	//---------------------------------------------------------------
	bool freeObject(T* objectPtr)
	{
		Node<T>* freeNode;
		int t1 = alignof(T);
		int t2 = alignof(ULONGLONG);
		if (alignof(T) > alignof(ULONGLONG))
		{
			int remainAlign = alignof(T) - alignof(ULONGLONG);
			freeNode = (Node<T>*)((char*)objectPtr - remainAlign - sizeof(ULONGLONG));
		}
		else
			freeNode = (Node<T>*)((char*)objectPtr - sizeof(ULONGLONG));

		if constexpr (DebugMode)
		{
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
		}
		
		freeNode->next = top;
		top = freeNode;
		if (!bPreConstructor)
			objectPtr->~T();
		poolCnt++;
		allocCnt--;
		return true;
	}

	inline ULONG GetPoolCnt() { return poolCnt; }
	inline ULONG GetAllocCnt() { return allocCnt; }

private:
	Node<T>* top;
	bool bPreConstructor;
	USHORT poolSeed;
	ULONG allocCnt;
	ULONG poolCnt;
	std::function<T* (void*)> creatorFunc;
};


template<typename T, bool DebugMode = false>
class CObjectPool_Lock
{
	template<typename, bool> friend class CObjectPool_ST;
	template<typename, bool> friend class CObjectPool_TLS;
	template<typename, bool> friend class CObjectPool_LF;

public:
	template<typename... Args>
	CObjectPool_Lock(bool preConstructor = true, int poolNum = 0, USHORT seed = 0, Args&&... args)
	{
		poolSeed = seed;
		bPreConstructor = preConstructor;
		top = nullptr;
		allocCnt = 0;
		poolCnt = 0;
		creatorFunc = [args...](void* instance)-> T* {
			return new(instance) T(args...);
		};
		InitializeSRWLock(&poolLock);

		for (int i = 0; i < poolNum; i++)
		{
			Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = top;
			top = newNode;
			if (bPreConstructor)
				creatorFunc(&newNode->instance);
		}
		poolCnt = poolNum;
		return;
	}

	~CObjectPool_Lock()
	{
		while (top != nullptr)
		{
			Node<T>* deleteNode = top;
			top = top->next;
			delete deleteNode;
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
	T* allocObject()
	{
		AcquireSRWLockExclusive(&poolLock);
		if (top == nullptr)
		{
			Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = nullptr;

			T* instance = &newNode->instance;
			creatorFunc(instance);
			allocCnt++;
			ReleaseSRWLockExclusive(&poolLock);
			return instance;
		}
		Node<T>* allocNode = top;
		top = allocNode->next;
		T* instance = &allocNode->instance;
		if (!bPreConstructor)
			creatorFunc(instance);
		poolCnt--;
		allocCnt++;
		ReleaseSRWLockExclusive(&poolLock);
		return instance;	
	}

	//---------------------------------------------------------------
	// 반납 정책
	// - bPreConstructor가 true인 경우, freeObject마다 소멸자 호출 X
	// - bPreConstructor가 false인 경우, freeObject마다 소멸자 호출 O
	// 
	// 안전 장치
	// - Node의 seed가 Pool의 seed와 동일한지 확인
	// - Node안의 instance 앞 뒤로 fence를 두어 값이 오염되어있는지 확인
	//---------------------------------------------------------------
	bool freeObject(T* objectPtr)
	{
		Node<T>* freeNode;
		int t1 = alignof(T);
		int t2 = alignof(ULONGLONG);
		if (alignof(T) > alignof(ULONGLONG))
		{
			int remainAlign = alignof(T) - alignof(ULONGLONG);
			freeNode = (Node<T>*)((char*)objectPtr - remainAlign - sizeof(ULONGLONG));
		}
		else
			freeNode = (Node<T>*)((char*)objectPtr - sizeof(ULONGLONG));

		if constexpr (DebugMode)
		{
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
		}

		AcquireSRWLockExclusive(&poolLock);
		freeNode->next = top;
		top = freeNode;
		if (!bPreConstructor)
			objectPtr->~T();
		poolCnt++;
		allocCnt--;
		ReleaseSRWLockExclusive(&poolLock);
		return true;
	}

	inline ULONG GetPoolCnt() { return poolCnt; }
	inline ULONG GetAllocCnt() { return allocCnt; }

private:
	Node<T>* top;
	bool bPreConstructor;
	USHORT poolSeed;
	ULONG allocCnt;
	ULONG poolCnt;
	std::function<T* (void*)> creatorFunc;
	SRWLOCK poolLock;
};



template<typename T, bool DebugMode = false>
class CObjectPool_LF
{
	template<typename, bool> friend class CObjectPool_ST;
	template<typename, bool> friend class CObjectPool_Lock;
	template<typename, bool> friend class CObjectPool_TLS;

public:
	template<typename... Args>
	CObjectPool_LF(bool preConstructor = true, int poolNum = 0, USHORT seed = 0, Args&&... args)
	{
		poolSeed = seed;
		bPreConstructor = preConstructor;
		top = nullptr;
		allocCnt = 0;
		poolCnt = 0;
		creatorFunc = [args...](void* instance)-> T* {
			return new(instance) T(args...);
		};

		for (int i = 0; i < poolNum; i++)
		{
			Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
			newNode->headFence = dfFenceValue;
			newNode->tailFence = dfFenceValue;
			newNode->seed = poolSeed;
			newNode->next = top;
			top = PackingNode(newNode, GetNodeStamp(top) + 1);
			if (bPreConstructor)
				creatorFunc(&newNode->instance);
		}
		poolCnt = poolNum;
	}

	~CObjectPool_LF()
	{
		Node<T>* maskedT = UnpackingNode(top);
		while (maskedT != nullptr)
		{
			Node<T>* deleteNode = maskedT;
			maskedT = maskedT->next;
			delete deleteNode;
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
	T* allocObject()
	{
		PRO_BEGIN("LF_Alloc");

		Node<T>* t;
		Node<T>* nextTop;
		Node<T>* maskedT;
		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			if (maskedT == nullptr)
			{
				Node<T>* newNode = (Node<T>*)malloc(sizeof(Node<T>));
				newNode->headFence = dfFenceValue;
				newNode->tailFence = dfFenceValue;
				newNode->seed = poolSeed;
				newNode->next = nullptr;

				T* instance = &newNode->instance;
				creatorFunc(instance);

				InterlockedIncrement(&allocCnt);
				return instance;
			}
			nextTop = PackingNode(maskedT->next, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);

		T* instance = &maskedT->instance;
		if (!bPreConstructor)
			creatorFunc(instance);
		
		InterlockedDecrement(&poolCnt);
		InterlockedIncrement(&allocCnt);
		
		PRO_END("LF_Alloc");
		return instance;
	}

	//---------------------------------------------------------------
	// 반납 정책
	// - bPreConstructor가 true인 경우, freeObject마다 소멸자 호출 X
	// - bPreConstructor가 false인 경우, freeObject마다 소멸자 호출 O
	// 
	// 안전 장치
	// - Node의 seed가 Pool의 seed와 동일한지 확인
	// - Node안의 instance 앞 뒤로 fence를 두어 값이 오염되어있는지 확인
	//---------------------------------------------------------------
	bool freeObject(T* objectPtr)
	{
		PRO_BEGIN("LF_Free");

		Node<T>* freeNode;
		int t1 = alignof(T);
		int t2 = alignof(ULONGLONG);
		if (alignof(T) > alignof(ULONGLONG))
		{
			int remainAlign = alignof(T) - alignof(ULONGLONG);
			freeNode = (Node<T>*)((char*)objectPtr - remainAlign - sizeof(ULONGLONG));
		}
		else
			freeNode = (Node<T>*)((char*)objectPtr - sizeof(ULONGLONG));

		if constexpr (DebugMode)
		{
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
		}

		if (!bPreConstructor)
			objectPtr->~T();

		Node<T>* t;
		Node<T>* nextTop;
		Node<T>* maskedT;
		do
		{
			t = top;
			maskedT = UnpackingNode(t);
			freeNode->next = maskedT;
			nextTop = PackingNode(freeNode, GetNodeStamp(t) + 1);
		} while (InterlockedCompareExchangePointer((void* volatile*)&top, nextTop, t) != t);
		
		InterlockedIncrement(&poolCnt);
		InterlockedDecrement(&allocCnt);

		PRO_END("LF_Free");
		return true;
	}

	inline ULONG GetPoolCnt() { return poolCnt; }
	inline ULONG GetAllocCnt() { return allocCnt; }

	inline Node<T>* PackingNode(Node<T>* ptr, ULONGLONG stamp) { return (Node<T>*)((ULONGLONG)ptr | (stamp << stampShift)); }
	inline Node<T>* UnpackingNode(Node<T>* ptr) { return (Node<T>*)((ULONGLONG)ptr & nodeMask); }
	inline ULONGLONG GetNodeStamp(Node<T>* ptr) { return (ULONGLONG)ptr >> stampShift; }

private:
	Node<T>* top;
	bool bPreConstructor;
	USHORT poolSeed;
	ULONG allocCnt;
	ULONG poolCnt;
	std::function<T* (void*)> creatorFunc;

	//--------------------------------------------
	// Node*의 하위 47비트 추출할 마스크
	//--------------------------------------------
	static const ULONGLONG nodeMask = (1ULL << 47) - 1;
	static const ULONG stampShift = 47;
};


template<typename T, bool DebugMode = false>
class CObjectPool_TLS
{
	template<typename, bool> friend class CObjectPool_ST;
	template<typename, bool> friend class CObjectPool_Lock;
	template<typename, bool> friend class CObjectPool_LF;

private:
	class CChunk
	{
		template<typename, bool> friend class CObjectPool_TLS;
	public:
		CChunk()
		{
			__debugbreak();
		}

		CChunk(bool preConstructor, USHORT poolSeed, USHORT numOfObject, std::function<T* (void*)> creatorFunc)
		{
			chunkNode = new Node<T>*[dfNumOfChunkObject];
			bPreConstructor = preConstructor;
			topIndex = numOfObject - 1;
			for (int i = 0; i < numOfObject; i++)
			{
				chunkNode[i] = (Node<T>*)malloc(sizeof(Node<T>));
				chunkNode[i]->headFence = dfFenceValue;
				chunkNode[i]->tailFence = dfFenceValue;
				chunkNode[i]->seed = poolSeed;
				if (bPreConstructor)
					creatorFunc(&chunkNode[i]->instance);
			}
			return;
		}
		~CChunk()
		{
			for (int i = 0; i <= topIndex; i++)
				delete chunkNode[i];
			delete[] chunkNode;
		}

		//----------------------------------------
		// Pop
		// - 현재 Chunk에 오브젝트가 없는 경우 nullptr 반환
		//----------------------------------------
		inline Node<T>* Pop()
		{
			if (topIndex == -1)
				return nullptr;
			return chunkNode[topIndex--];
		}

		//---------------------------------------
		// Push
		// - 현재 Chunk에 오브젝트가 가득 찬 경우 false 반환
		//---------------------------------------
		inline bool Push(Node<T>* node)
		{
			if (topIndex >= dfNumOfChunkObject - 1)
				return false;
			chunkNode[++topIndex] = node;
			return true;
		}

		inline ULONG GetNodeCnt() { return topIndex + 1; }

	private:
		Node<T>** chunkNode;
		SHORT topIndex;
		bool bPreConstructor;
	};


public:
	template<typename... Args>
	CObjectPool_TLS(bool preConstructor = true, int poolNum = 0, USHORT seed = 0, Args&&... args)
	{
		tlsIndex = TlsAlloc();
		if (tlsIndex == TLS_OUT_OF_INDEXES)
		{
			_LOG(dfLOG_LEVEL_SYSTEM, L"TLS_OUT_OF_INDEXES\n");
			__debugbreak();
			return;
		}
		poolSeed = seed;
		bPreConstructor = preConstructor;
		allocCnt = 0;
		creatorFunc = [args...](void* instance)-> T* {
			return new(instance) T(args...);
		};
		chunkPool = new CObjectPool_LF<CChunk, false>(true, poolNum, poolSeed, preConstructor, poolSeed, dfNumOfChunkObject, creatorFunc);
		emptyChunkPool = new CObjectPool_LF<CChunk, false>(true, poolNum, poolSeed, preConstructor, poolSeed, 0, creatorFunc);
	}
	~CObjectPool_TLS()
	{
		delete chunkPool;
		delete emptyChunkPool;
	}

	//---------------------------------------------------------------
	// 할당 정책
	// - bPreConstructor가 true인 경우, allocObject마다 생성자 호출 X
	// - bPreConstructor가 false인 경우, allocObject마다 생성자 호출 O
	//---------------------------------------------------------------
	T* allocObject()
	{
		PRO_BEGIN("TLS_Alloc");
		CObjectPool_ST<CChunk>* localChunkPool = GetLocalChunkPool();
	
		//--------------------------------------------------------------------------
		// localChunkPool에 청크가 없으면 (공용)ChunkPool에서 청크 가져오기
		//--------------------------------------------------------------------------
		if (localChunkPool->top == nullptr)
			localChunkPool->freeObject(chunkPool->allocObject());
		
		//-------------------------------------------------------------------------
		// 오브젝트 할당
		// - 할당 후, 청크에 오브젝트가 없으면 빈 청크를 (공용)emptyChunkPool로 이동
		//-------------------------------------------------------------------------
		CChunk* chunk = &localChunkPool->top->instance;
		T* instance = &chunk->Pop()->instance;
		if (chunk->topIndex == -1)
			emptyChunkPool->freeObject(localChunkPool->allocObject());
		
		if (!bPreConstructor)
			creatorFunc(instance);

		InterlockedIncrement(&allocCnt);

		PRO_END("TLS_Alloc");
		return instance;
	}


	//---------------------------------------------------------------
	// 반납 정책
	// - bPreConstructor가 true인 경우, freeObject마다 소멸자 호출 X
	// - bPreConstructor가 false인 경우, freeObject마다 소멸자 호출 O
	// 
	// 안전 장치 [ dfDebugObjectPool가 정의되어 있을 경우 반납 시 노드 검증 수행 ]
	// - Node의 seed가 Pool의 seed와 동일한지 확인
	// - Node안의 instance 앞 뒤로 fence를 두어 값이 오염되어있는지 확인
	//---------------------------------------------------------------
	bool freeObject(T* objectPtr)
	{
		PRO_BEGIN("TLS_Free");	
	
		Node<T>* freeNode;
		int t1 = alignof(T);
		int t2 = alignof(ULONGLONG);
		if (alignof(T) > alignof(ULONGLONG))
		{
			int remainAlign = alignof(T) - alignof(ULONGLONG);
			freeNode = (Node<T>*)((char*)objectPtr - remainAlign - sizeof(ULONGLONG));
		}
		else
			freeNode = (Node<T>*)((char*)objectPtr - sizeof(ULONGLONG));

		if constexpr (DebugMode)
		{
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
		}

		CObjectPool_ST<CChunk>* localChunkPool = GetLocalChunkPool();

		//----------------------------------------------------------------------------------------
		// localChunkPool에 청크가 없는 경우 (공용)emptyChunkPool에서 빈 청크를 가져와서 Push 수행
		//----------------------------------------------------------------------------------------
		if (localChunkPool->top == nullptr)
			localChunkPool->freeObject(emptyChunkPool->allocObject());
		
		//--------------------------------------------------------------------------
		// 청크에 노드 반납
		// - 청크에 노드가 가득찬 경우 (공용)emptyChunkPool에서 빈 청크를 가져오기
		// 
		// - 빈 청크를 Push하기 전에 여분의 청크가 있다면 (공용)ChunkPool에 청크 반납
		// - 따라서 모든 스레드의 청크 풀에는 최대 2개 청크 소유 가능
		//---------------------------------------------------------------------------
		bool pushRet = localChunkPool->top->instance.Push(freeNode);
		if (pushRet == false)
		{
			if (localChunkPool->GetPoolCnt() > 1)
				chunkPool->freeObject(localChunkPool->allocObject());
			
			localChunkPool->freeObject(emptyChunkPool->allocObject());
			localChunkPool->top->instance.Push(freeNode);
		}

		if (!bPreConstructor)
			objectPtr->~T();

		InterlockedDecrement(&allocCnt);

		PRO_END("TLS_Free");
		return true;
	}

	inline ULONG GetAllocCnt() { return allocCnt; };

	inline ULONG GetChunkPoolCnt() { return chunkPool->GetPoolCnt(); };
	inline ULONG GetEmptyPoolCnt() { return emptyChunkPool->GetPoolCnt(); };

private:
	inline CObjectPool_ST<CChunk>* GetLocalChunkPool()
	{
		CObjectPool_ST<CChunk>* localPool = (CObjectPool_ST<CChunk>*)TlsGetValue(tlsIndex);
		if (localPool == nullptr)
		{
			localPool = new CObjectPool_ST<CChunk>(true, 0, poolSeed);
			TlsSetValue(tlsIndex, localPool);
		}
		return localPool;
	}

private:
	DWORD tlsIndex;
	USHORT poolSeed;
	bool bPreConstructor;
	ULONG allocCnt;
	std::function<T*(void*)> creatorFunc;
	
	CObjectPool_LF<CChunk, false>* chunkPool;
	CObjectPool_LF<CChunk, false>* emptyChunkPool;
};
