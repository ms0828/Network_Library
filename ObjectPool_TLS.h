#pragma once

#define PROFILE

#include <Windows.h>
#include <new>
#include <utility>
#include "LockFreeStack.h"
#include "Log.h"

#define PROFILE
#include "Profiler.h"

#define dfFenceValue 0xdddddddddddddddd

#define dfDebugObjectPool

#define dfNumOfChunkNode 100

template<typename T>
class CObjectPool_TLS
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

	friend class CThreadChunkPool;


	class CChunk
	{
	public:
		template<typename... Args>
		CChunk(CObjectPool_TLS<T>* _owner, ULONG numOfChunkNode, Args&&... args)
		{
			top = nullptr;
			next = nullptr;
			owner = _owner;
			for (int i = 0; i < numOfChunkNode; i++)
			{
				Node* newNode = (Node*)malloc(sizeof(Node));
				newNode->headFence = dfFenceValue;
				newNode->tailFence = dfFenceValue;
				newNode->seed = owner->poolSeed;
				newNode->next = top;
				top = newNode;

				//--------------------------------------------------------
				// preConstructor가 true인 경우, 처음 생성 시 생성자 호출
				//--------------------------------------------------------
				if (owner->bPreConstructor)
				{
					T* instance = &newNode->instance;
					new (instance) T(std::forward<Args>(args)...);
				}
			}
			nodeCnt = numOfChunkNode;
		}

		CChunk(CObjectPool_TLS<T>* _owner)
		{
			top = nullptr;
			next = nullptr;
			owner = _owner;
			nodeCnt = 0;
		}



		~CChunk()
		{
			while (top != nullptr)
			{
				Node* deleteNode = top;
				top = top->next;

				//------------------------------------------------------------------------------
				// bPreConstructor가 true인 경우, ObjectPool이 소멸될 때 instance의 소멸자 호출
				//------------------------------------------------------------------------------
				if (owner->bPreConstructor)
					delete deleteNode;
				else
					free(deleteNode);
			}
		}

		//----------------------------------------
		// Pop
		// - 현재 Chunk에 노드가 없는 경우 false 반환
		//----------------------------------------
		bool Pop(Node*& node)
		{
			if (top == nullptr)
				return false;

			node = top;
			top = top->next;
			nodeCnt--;

			_LOG(dfLOG_LEVEL_DEBUG, L"[CChunkNode] [Pop] : NodeAdr = %016llx / Chunk = %016llx / Node Cnt = %d\n", node, this, nodeCnt);
			return true;
		}

		//---------------------------------------
		// Push
		// - 현재 Chunk에 노드가 가득 찬 경우 false 반환
		//---------------------------------------
		bool Push(Node*& node)
		{
			if (nodeCnt >= dfNumOfChunkNode)
				return false;

			node->next = top;
			top = node;
			nodeCnt++;

			_LOG(dfLOG_LEVEL_DEBUG, L"[CChunkNode] [Push] : NodeAdr = %016llx / Chunk = %016llx / Node Cnt = %d\n", node, this, nodeCnt);
			return true;
		}


		inline ULONG GetNodeCnt()
		{
			return nodeCnt;
		}


	private:
		Node* top;
		ULONG nodeCnt;
		CObjectPool_TLS<T>* owner;
		CChunk* next;
		friend class CThreadChunkPool;
	};


	class CThreadChunkPool
	{
	public:

		CThreadChunkPool()
		{
			top = nullptr;
			chunkCnt = 0;
		}

		~CThreadChunkPool()
		{
			while (top != nullptr)
			{
				CChunk* deleteChunkNode = top;
				top = top->next;
				delete deleteChunkNode;
			}
		}

		//---------------------------------------
		// Top
		// - Chunk가 없는 경우 false 반환
		//---------------------------------------
		inline bool Top(CChunk*& chunk)
		{
			if (top == nullptr)
				return false;
			chunk = top;
			return true;
		}

		//---------------------------------------
		// Pop
		// - Chunk가 없는 경우 false 반환
		//---------------------------------------
		inline bool Pop(CChunk*& chunk)
		{
			if (top == nullptr)
				return false;

			chunk = top;
			top = top->next;
			chunkCnt--;

			_LOG(dfLOG_LEVEL_DEBUG, L"[CThreadLocalChunkPool] [Pop] : ChunkAdr = %016llx / Chunk Cnt = %d\n", chunk, chunkCnt);
			return true;
		}

		//---------------------------------------
		// Push
		//---------------------------------------
		inline bool Push(CChunk*& chunk)
		{
			chunk->next = top;
			top = chunk;
			chunkCnt++;

			_LOG(dfLOG_LEVEL_DEBUG, L"[CThreadLocalChunkPool] [Push] : ChunkAdr = %016llx / Chunk Cnt = %d\n", chunk, chunkCnt);
			return true;
		}

		inline ULONG GetChunkCnt()
		{
			return chunkCnt;
		}

	private:
		CChunk* top;
		ULONG chunkCnt;

		friend class CObjectPool_TLS;
	};



public:
	template<typename... Args>
	CObjectPool_TLS(bool preConstructor, int poolNum = 0, Args&&... args)
	{
		tlsIndex = TlsAlloc();
		if (tlsIndex == TLS_OUT_OF_INDEXES)
		{
			_LOG(dfLOG_LEVEL_SYSTEM, L"TLS_OUT_OF_INDEXES\n");
			return;
		}
		initPoolNum = poolNum;
		poolSeed = rand();
		bPreConstructor = preConstructor;

	}
	~CObjectPool_TLS()
	{

	}

	//---------------------------------------------------------------
	// 할당 정책
	// - bPreConstructor가 true인 경우, allocObject마다 생성자 호출 X
	// - bPreConstructor가 false인 경우, allocObject마다 생성자 호출 O
	//---------------------------------------------------------------
	template<typename... Args>
	T* allocObject(Args&&... args)
	{
		PRO_BEGIN("Alloc");
		CThreadChunkPool* threadChunkPool = GetThreadChunkPool();
		CChunk* chunk = threadChunkPool->top;

		//--------------------------------------------------------------
		// TLS 청크 풀에 청크가 없으면 공용 풀에서 청크 가져와서 TLS 청크 풀에 Push
		// - 공용 풀에 청크가 없다면 청크를 새로 생성 후 TLS 청크 풀에 Push
		//--------------------------------------------------------------
		
		if (chunk == nullptr)
		{
			PRO_BEGIN("[Alloc Object] threadChunkPool is Empty - Access ChunkPool");
			bool popRet = chunkPool.Pop(chunk);
			if (popRet == false)
			{
				PRO_BEGIN("[Alloc Object] threadChunkPool is Empty - New Chunk");
				chunk = new CChunk(this, dfNumOfChunkNode, std::forward<Args>(args)...);
				PRO_END("[Alloc Object] threadChunkPool is Empty - New Chunk");
				_LOG(dfLOG_LEVEL_DEBUG, L"[CObjectPool_TLS] [AllocObject - Create New Chunk] : ChunkAdr = %016llx \n", chunk);
			}
			else // 디버깅용 로그 (나중에 지우기)
			{
				_LOG(dfLOG_LEVEL_DEBUG, L"[CObjectPool_TLS] [AllocObject - Get Chunk From Public Pool] : ChunkAdr = %016llx / Chunk Pool Cnt = %d \n", chunk, chunkPool.GetSize());
			}
			threadChunkPool->Push(chunk);
			PRO_END("[Alloc Object] threadChunkPool is Empty - Access ChunkPool");
		}


		//----------------------------------------------------
		// 오브젝트 노드 할당
		// - 할당 후, 청크에 노드가 없으면 (공용)emptyChunkPool로 이동
		//----------------------------------------------------
		Node* objectNode;
		bool popRet = chunk->Pop(objectNode);
		if (popRet == false) // 디버깅용 - 이 조건이 타면 잘못된 것
			__debugbreak();

		
		if (chunk->GetNodeCnt() == 0)
		{
			PRO_BEGIN("[Alloc Object] after Alloc, Node is Empty - Access EmptyChunkPool");
			CChunk* emptyChunk;
			bool popRet = threadChunkPool->Pop(emptyChunk);
			if (popRet == false)
				__debugbreak();
			if(chunk != emptyChunk)
				__debugbreak();

			emptyChunkPool.Push(chunk);
			PRO_END("[Alloc Object] after Alloc, Node is Empty - Access EmptyChunkPool");
			_LOG(dfLOG_LEVEL_DEBUG, L"[CObjectPool_TLS] [AllocObject - Push Emtpy Chunk] : ChunkAdr = %016llx / Empty Chunk Pool Cnt = %d \n", chunk, emptyChunkPool.GetSize());
		}
		
		T* instance = &objectNode->instance;
		if (!bPreConstructor)
			new (instance) T(std::forward<Args>(args)...);
		_LOG(dfLOG_LEVEL_DEBUG, L"[CObjectPool_TLS] [AllocObject - Complete Node Alloc] : ChunkAdr = %016llx / NodeAdr = %016llx / threadChunkPool Cnt = %d\n", chunk, objectNode, threadChunkPool->GetChunkCnt());
		
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

		CThreadChunkPool* threadChunkPool = GetThreadChunkPool();
		if (threadChunkPool == nullptr)
		{
			_LOG(dfLOG_LEVEL_SYSTEM, L"freeObject : TlsGetValue return nullptr\n");
			__debugbreak();
			return false;
		}
		CChunk* chunk = threadChunkPool->top;
		
		
		//------------------------------------------------------------
		// 스레드 청크 풀에 청크가 없는 경우 빈 청크를 가져와서 Push 수행
		// - (공용)emptyChunkPool에서 빈 청크를 가져오기 시도
		// - (공용)emptyChunkPool에 빈 청크가 없는 경우 새로운 빈 청크 생성
		//------------------------------------------------------------
		if (chunk == nullptr)
		{
			PRO_BEGIN("[FreeNode] ThreadChunkPool is Empty - Access Empty Pool");
			bool popRet = emptyChunkPool.Pop(chunk);
			if (popRet == false)
				chunk = new CChunk(this);
			
			threadChunkPool->Push(chunk);
			PRO_END("[FreeNode] ThreadChunkPool is Empty - Access Empty Pool");
			_LOG(dfLOG_LEVEL_SYSTEM, L"[CObjectPool_TLS] [Borrow Empty Chunk From Empty Chunk Pool] : ChunkAdr = %016llx / chunkPool Cnt = %d\n", chunk, chunkPool.GetSize());
		}
	


		//--------------------------------------------------------------------------
		// 청크에 노드 삽입
		// - 청크에 노드가 가득찬 경우 (공용)emptyChunkPool에서 빈 청크를 가져오기
		// - (공용)emptyChunkPool에 빈 청크가 없는 경우 새로운 빈 청크 생성
		// 
		// - 빈 청크를 Push하기 전에 여분의 청크가 있다면 (공용)ChunkPool에 청크 반납
		// - 따라서 모든 스레드의 청크 풀에는 최대 2개 청크 소유 가능
		//---------------------------------------------------------------------------
		bool pushRet = chunk->Push(freeNode);
		
		if (pushRet == false)
		{
			PRO_BEGIN("[FreeNode] Chunk is Full - Access Empty Pool");
			bool popRet = emptyChunkPool.Pop(chunk);
			if (popRet == false)
			{
				chunk = new CChunk(this);
				_LOG(dfLOG_LEVEL_SYSTEM, L"[CObjectPool_TLS] [Create Empty Chunk] : ChunkAdr = %016llx / chunkPool Cnt = %d\n", chunk, chunkPool.GetSize());
			}

			if (threadChunkPool->GetChunkCnt() > 2)
			{
				PRO_BEGIN("FreeNode(after) - ChunkCnt > 2 - Access Chunk Pool");
				CChunk* freeChunk;
				bool popRet = threadChunkPool->Pop(freeChunk);
				if (popRet == false)
					__debugbreak(); // 디버깅용

				chunkPool.Push(freeChunk);

				PRO_END("FreeNode(after) - ChunkCnt > 2 - Access Chunk Pool");
				_LOG(dfLOG_LEVEL_SYSTEM, L"[CObjectPool_TLS] [FreeObject - freeChunk] : ChunkAdr = %016llx / chunkPool Cnt = %d\n", freeChunk, chunkPool.GetSize());
			}
			threadChunkPool->Push(chunk);
			bool pushRet = chunk->Push(freeNode);
			if (pushRet == false || chunk->GetNodeCnt() != 1)
				__debugbreak();

			PRO_END("[FreeNode] Chunk is Full - Access Empty Pool");
		}
		
		if (!bPreConstructor)
			objectPtr->~T();

		_LOG(dfLOG_LEVEL_DEBUG, L"[CObjectPool_TLS] [FreeObject - Complete Node Free] : ChunkAdr = %016llx  / NodeAdr = %016llx / threadChunkPool Cnt = %d\n", chunk, freeNode, threadChunkPool->GetChunkCnt());
		PRO_END("Free");
		return true;
	}


private:
	inline CThreadChunkPool* GetThreadChunkPool()
	{
		CThreadChunkPool* localPool = (CThreadChunkPool*)TlsGetValue(tlsIndex);
		if (localPool == nullptr)
		{
			localPool = new CThreadChunkPool();
			TlsSetValue(tlsIndex, localPool);
		}
		return localPool;
	}

private:
	bool bPreConstructor;
	USHORT poolSeed;
	ULONG initPoolNum;
	DWORD tlsIndex;

	//--------------------------------
	// 공용 풀
	//--------------------------------
public:
	CLockFreeStack<CChunk*> chunkPool;
	CLockFreeStack<CChunk*> emptyChunkPool;

};