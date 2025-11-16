#pragma once
#include "Windows.h"
#include "ObjectPool.h"

#define DEFAULT_BUFSIZE 1024

template <typename T, bool DebugMode>
class CObjectPool_TLS;

template <typename T, bool DebugMode>
class CObjectPool_LF;

class CLanServer;
class CLanClient;
class CNetServer;
class CNetClient;
class CPacketViewer;

class CPacket
{
	friend class CLanServer;
	friend class CLanClient;
	friend class CNetServer;
	friend class CNetClient;
	friend class CPacketViewer;
	template<typename, bool> friend class CObjectPool_TLS;
	template<typename, bool> friend class CObjectPool_LF;

private:
	//--------------------------------------------------------------
	// CPacket()
	// - 기본 생성자는 bufferSize가 DefaultSize로 세팅 
	//--------------------------------------------------------------
	CPacket()
	{
		buffer = (char*)malloc(sizeof(char) * DEFAULT_BUFSIZE);
		bufferMaxSize = DEFAULT_BUFSIZE;
		useSize = 0;
		readPtr = buffer;
		writePtr = buffer;
		refCount = 0;
	}

	//--------------------------------------------------------------
	// CPacket(int iBufferSize)
	// - bufferSize를 iBufferSize로 세팅
	// - bufferSize가 0이면 해당 직렬화 버퍼는 다른 버퍼를 감싸는 rapper(읽기 전용)로 작동 
	//--------------------------------------------------------------
	CPacket(int iBufferSize)
	{
		buffer = (char*)malloc(sizeof(char) * iBufferSize);
		bufferMaxSize = iBufferSize;
		useSize = 0;
		readPtr = buffer;
		writePtr = buffer;
		refCount = 0;
	}

public:
	~CPacket()
	{
		readPtr = nullptr;
		writePtr = nullptr;
		free(buffer);
	}

	//--------------------------------------------------------------
	// 데이터 얻기 (Dequeue)
	//--------------------------------------------------------------
	int	GetData(char* chpDest, int iSize);

	//--------------------------------------------------------------
	// PeekData
	// - 데이터 얻기 (Peek)
	//--------------------------------------------------------------
	int	PeekData(char* chpDest, int iSize);

	//--------------------------------------------------------------
	// PutData
	// - 데이터 삽입
	//--------------------------------------------------------------
	int	PutData(char* chpSrc, int iSrcSize);

	//--------------------------------------------------------------
	// Clear
	// - 직렬화 버퍼 초기화
	//--------------------------------------------------------------
	inline void Clear(void)
	{
		readPtr = buffer;
		writePtr = buffer;
		useSize = 0;
	}

	//--------------------------------------------------------------
	// MoveWritePos
	// - 이동한 크기 반환
	// - buffer를 벗어나면 0 반환
	//--------------------------------------------------------------
	inline int MoveWritePos(int iSize)
	{
		if (writePtr + iSize > buffer + bufferMaxSize)
			return 0;
		writePtr = writePtr + iSize;
		useSize += iSize;
		return iSize;
	}

	//--------------------------------------------------------------
	// MoveReadPos
	// - 이동한 크기 반환
	// - buffer를 벗어나면 0 반환
	//--------------------------------------------------------------
	inline int MoveReadPos(int iSize)
	{
		if (readPtr + iSize > buffer + bufferMaxSize)
			return 0;
		readPtr = readPtr + iSize;
		useSize -= iSize;
		return iSize;
	}

	//----------------------------------------------------------
	// AllocSendPacket
	// - sendPacketPool에서 send용 직렬화 버퍼를 할당
	// - RefCount 증가
	// - 사용 끝난 후 ReleaseSendPacket()호출 필요
	//----------------------------------------------------------
	static CPacket* AllocSendPacket();

	//----------------------------------------------------------
	// AllocRecvPacket
	// - recvPacketPool에서 recv용 직렬화 버퍼를 할당
	// - RefCount 증가
	// - 사용 끝난 후 ReleaseRecvPacket()호출 필요
	//----------------------------------------------------------
	static CPacket* AllocRecvPacket();

	//----------------------------------------------------------
	// ReleaseSendPacket
	// - RefCount 감소
	// - 감소된 RefCount값이 0이면 풀에 반납
	//----------------------------------------------------------
	static void ReleaseSendPacket(CPacket* packet);

	//----------------------------------------------------------
	// ReleaseRecvPacket
	// - RefCount 감소
	// - 감소된 RefCount값이 0이면 풀에 반납
	//----------------------------------------------------------
	static void ReleaseRecvPacket(CPacket* packet);




	//--------------------------------------------------------------
	// 연산자 오버로딩
	//--------------------------------------------------------------
	CPacket& operator = (CPacket& clSrcPacket);
	inline CPacket& operator << (unsigned char byValue)
	{
		*writePtr = byValue;
		writePtr += sizeof(unsigned char);
		useSize += sizeof(unsigned char);
		return *this;
	}
	inline CPacket& operator << (char chValue)
	{
		*writePtr = chValue;
		writePtr += sizeof(char);
		useSize += sizeof(char);
		return *this;
	}
	inline CPacket& operator << (unsigned short wValue)
	{
		*(unsigned short*)writePtr = wValue;
		writePtr += sizeof(unsigned short);
		useSize += sizeof(unsigned short);
		return *this;
	}
	inline CPacket& operator << (short shValue)
	{
		*(short*)writePtr = shValue;
		writePtr += sizeof(short);
		useSize += sizeof(short);
		return *this;
	}
	inline CPacket& operator << (unsigned int iValue)
	{
		*(unsigned int*)writePtr = iValue;
		writePtr += sizeof(unsigned int);
		useSize += sizeof(unsigned int);
		return *this;
	}
	inline CPacket& operator << (int iValue)
	{
		*(int*)writePtr = iValue;
		writePtr += sizeof(int);
		useSize += sizeof(int);
		return *this;
	}
	inline CPacket& operator << (unsigned long lValue)
	{
		*(unsigned long*)writePtr = lValue;
		writePtr += sizeof(unsigned long);
		useSize += sizeof(unsigned long);
		return *this;
	}
	inline CPacket& operator << (long lValue)
	{
		*(long*)writePtr = lValue;
		writePtr += sizeof(long);
		useSize += sizeof(long);
		return *this;
	}
	inline CPacket& operator << (float fValue)
	{
		*(float*)writePtr = fValue;
		writePtr += sizeof(float);
		useSize += sizeof(float);
		return *this;
	}
	inline CPacket& operator << (double dValue)
	{
		*(double*)writePtr = dValue;
		writePtr += sizeof(double);
		useSize += sizeof(double);
		return *this;
	}
	inline CPacket& operator << (unsigned __int64 iValue)
	{
		*(unsigned __int64*)writePtr = iValue;
		writePtr += sizeof(unsigned __int64);
		useSize += sizeof(unsigned __int64);
		return *this;
	}
	inline CPacket& operator << (__int64 iValue)
	{
		*(__int64*)writePtr = iValue;
		writePtr += sizeof(__int64);
		useSize += sizeof(__int64);
		return *this;
	}


	inline CPacket& operator >> (unsigned char& byValue)
	{
		byValue = *(unsigned char*)readPtr;
		readPtr += sizeof(unsigned char);
		useSize -= sizeof(unsigned char);
		return *this;
	}
	inline CPacket& operator >> (char& chValue)
	{
		chValue = *(char*)readPtr;
		readPtr += sizeof(char);
		useSize -= sizeof(char);
		return *this;
	}
	inline CPacket& operator >> (unsigned short& wValue)
	{
		wValue = *(unsigned short*)readPtr;
		readPtr += sizeof(unsigned short);
		useSize -= sizeof(unsigned short);
		return *this;
	}
	inline CPacket& operator >> (short& shValue)
	{
		shValue = *(short*)readPtr;
		readPtr += sizeof(short);
		useSize -= sizeof(short);
		return *this;
	}
	inline CPacket& operator >> (unsigned int& iValue)
	{
		iValue = *(unsigned int*)readPtr;
		readPtr += sizeof(unsigned int);
		useSize -= sizeof(unsigned int);
		return *this;
	}
	inline CPacket& operator >> (int& iValue)
	{
		iValue = *(int*)readPtr;
		readPtr += sizeof(int);
		useSize -= sizeof(int);
		return *this;
	}
	inline CPacket& operator >> (unsigned long& dwValue)
	{
		dwValue = *(unsigned long*)readPtr;
		readPtr += sizeof(unsigned long);
		useSize -= sizeof(unsigned long);
		return *this;
	}
	inline CPacket& operator >> (long& dwValue)
	{
		dwValue = *(long*)readPtr;
		readPtr += sizeof(long);
		useSize -= sizeof(long);
		return *this;
	}
	inline CPacket& operator >> (float& fValue)
	{
		fValue = *(float*)readPtr;
		readPtr += sizeof(float);
		useSize -= sizeof(float);
		return *this;
	}
	inline CPacket& operator >> (double& dValue)
	{
		dValue = *(double*)readPtr;
		readPtr += sizeof(double);
		useSize -= sizeof(double);
		return *this;
	}
	inline CPacket& operator >> (unsigned __int64& iValue)
	{
		iValue = *(unsigned __int64*)readPtr;
		readPtr += sizeof(unsigned __int64);
		useSize -= sizeof(unsigned __int64);
		return *this;
	}
	inline CPacket& operator >> (__int64& iValue)
	{
		iValue = *(__int64*)readPtr;
		readPtr += sizeof(__int64);
		useSize -= sizeof(__int64);
		return *this;
	}
	
	inline char* GetBufferPtr(void) { return buffer; }
	inline int GetBufferSize(void) { return bufferMaxSize; }
	inline int GetDataSize(void) { return useSize; }
	inline char* GetReadPtr() { return readPtr; };
	inline static ULONG GetSendPacketAllocCount() { return sendPacketPool.GetAllocCnt(); };
	inline static ULONG GetSendPacketChunkPoolCount() { return sendPacketPool.GetChunkPoolCnt(); };
	inline static ULONG GetSendPacketEmptyPoolCount() { return sendPacketPool.GetEmptyPoolCnt(); };
	inline static ULONG GetRecvPacketAllocCount() { return recvPacketPool.GetAllocCnt(); };
	inline static ULONG GetRecvPacketChunkPoolCount() { return recvPacketPool.GetChunkPoolCnt(); };
	inline static ULONG GetRecvPacketEmptyPoolCount() { return recvPacketPool.GetEmptyPoolCnt(); };


private:
	char* buffer;
	char* readPtr;
	char* writePtr;

	int	bufferMaxSize;
	int	useSize;
	ULONG refCount;

private:
	//static CObjectPool_LF<CPacket, true> sendPacketPool;
	//static CObjectPool_LF<CPacket, true> recvPacketPool;
	static CObjectPool_TLS<CPacket, true> sendPacketPool;
	static CObjectPool_TLS<CPacket, true> recvPacketPool;
};


class CPacketViewer
{
	friend class CLanServer;
	friend class CLanClient;
	friend class CNetServer;
	friend class CNetClient;

public:
	CPacketViewer()
	{
		packet = nullptr;
		readPtr = nullptr;
		useSize = 0;
		refCount = 0;
	}
	~CPacketViewer()
	{

	}


	//--------------------------------------------------------------
	// GetData
	// - 데이터 얻기 (Dequeue)
	//--------------------------------------------------------------
	int	GetData(char* chpDest, int iSize);

	//--------------------------------------------------------------
	// PeekData
	// - 데이터 얻기 (Peek)
	//--------------------------------------------------------------
	int	PeekData(char* chpDest, int iSize);

	//--------------------------------------------------------------
	// SetRefPacket
	// 인자로 받은 packet의 readPtr부터 dataSize만큼의 Viewer 세팅
	//--------------------------------------------------------------
	inline void SetRefPacket(CPacket* refPacket, ULONG dataSize)
	{
		packet = refPacket;
		readPtr = packet->readPtr;
		useSize = dataSize;
	}

	//----------------------------------------------------------
	// AllocPacketViewer
	// - packetViewerPool에서 viewer 할당
	// - viewer와 참조하는 CPacket의 RefCount 증가
	// - 사용 끝난 후 ReleasePacketViewer()호출 필요
	//----------------------------------------------------------
	static CPacketViewer* AllocPacketViewer(CPacket* refPacket, ULONG dataSize);

	//----------------------------------------------------------
	// ReleasePacketViewer
	// - RefCount 감소
	// - RefCount값이 0이면 Viewer 반납 및 참조하고 있는 packet을 대상으로 ReleaseRecvPacket 호출
	//----------------------------------------------------------
	static void ReleasePacketViewer(CPacketViewer* packetViewer);
	
	


	//--------------------------------------------------------------
	// 연산자 오버로딩
	//--------------------------------------------------------------
	inline CPacketViewer& operator >> (unsigned char& byValue)
	{
		byValue = *(unsigned char*)readPtr;
		readPtr += sizeof(unsigned char);
		useSize -= sizeof(unsigned char);
		return *this;
	}
	inline CPacketViewer& operator >> (char& chValue)
	{
		chValue = *(char*)readPtr;
		readPtr += sizeof(char);
		useSize -= sizeof(char);
		return *this;
	}
	inline CPacketViewer& operator >> (unsigned short& wValue)
	{
		wValue = *(unsigned short*)readPtr;
		readPtr += sizeof(unsigned short);
		useSize -= sizeof(unsigned short);
		return *this;
	}
	inline CPacketViewer& operator >> (short& shValue)
	{
		shValue = *(short*)readPtr;
		readPtr += sizeof(short);
		useSize -= sizeof(short);
		return *this;
	}
	inline CPacketViewer& operator >> (unsigned int& iValue)
	{
		iValue = *(unsigned int*)readPtr;
		readPtr += sizeof(unsigned int);
		useSize -= sizeof(unsigned int);
		return *this;
	}
	inline CPacketViewer& operator >> (int& iValue)
	{
		iValue = *(int*)readPtr;
		readPtr += sizeof(int);
		useSize -= sizeof(int);
		return *this;
	}
	inline CPacketViewer& operator >> (unsigned long& dwValue)
	{
		dwValue = *(unsigned long*)readPtr;
		readPtr += sizeof(unsigned long);
		useSize -= sizeof(unsigned long);
		return *this;
	}
	inline CPacketViewer& operator >> (long& dwValue)
	{
		dwValue = *(long*)readPtr;
		readPtr += sizeof(long);
		useSize -= sizeof(long);
		return *this;
	}
	inline CPacketViewer& operator >> (float& fValue)
	{
		fValue = *(float*)readPtr;
		readPtr += sizeof(float);
		useSize -= sizeof(float);
		return *this;
	}
	inline CPacketViewer& operator >> (double& dValue)
	{
		dValue = *(double*)readPtr;
		readPtr += sizeof(double);
		useSize -= sizeof(double);
		return *this;
	}
	inline CPacketViewer& operator >> (unsigned __int64& iValue)
	{
		iValue = *(unsigned __int64*)readPtr;
		readPtr += sizeof(unsigned __int64);
		useSize -= sizeof(unsigned __int64);
		return *this;
	}
	inline CPacketViewer& operator >> (__int64& iValue)
	{
		iValue = *(__int64*)readPtr;
		readPtr += sizeof(__int64);
		useSize -= sizeof(__int64);
		return *this;
	}

	
	inline ULONG IncrementRefCount() { return InterlockedIncrement(&refCount); };
	inline CPacket* GetPacketPtr() { return packet; };
	inline ULONG GetDataSize() { return useSize; };
	inline char* GetReadPtr() { return readPtr; };
	inline static ULONG GetPacketViewerAllocCount() { return packetViewerPool.GetAllocCnt(); };
	inline static ULONG GetPacketViewerChunkPoolCount() { return packetViewerPool.GetChunkPoolCnt(); };
	inline static ULONG GetPacketViewerEmptyPoolCount() { return packetViewerPool.GetEmptyPoolCnt(); };


private:
	CPacket* packet;
	char* readPtr;
	ULONG refCount;
	ULONG useSize;

	static CObjectPool_TLS<CPacketViewer, true> packetViewerPool;
};