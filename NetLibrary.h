#pragma once

#define PROFILE

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Process.h>
#include "RingBuffer.h"
#include "CPacket.h"
#include "Profiler.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "Log.h"
#pragma comment (lib, "ws2_32")
#pragma comment(lib, "winmm.lib")

#define dfMaxSendPacketCount 300

class CPacket;
class CPacketViewer;
class CRingBuffer;

enum EIOTYPE
{
	ERecv = 0,
	ESend = 1,
};
struct SessionOlp
{
	WSAOVERLAPPED overlapped;
	EIOTYPE type;
};
struct SendOlp
{
	SessionOlp olp;
	CPacket* freePackets[dfMaxSendPacketCount];
	ULONG sendCount;
};
struct RecvOlp
{
	SessionOlp olp;
};


//-----------------------------------------------
// 패킷 헤더 구조
//-----------------------------------------------
struct st_PacketHeader
{
	//USHORT packetType;
	USHORT payloadLen;
};


class Session
{
public:
	Session()
	{
		sock = INVALID_SOCKET;
		sessionId = 0;
		recvOlp.olp.type = ERecv;
		sendOlp.olp.type = ESend;
		sendLFQ = new CLockFreeQueue<CPacket*>();
		recvQ = nullptr;
		isSending = false;
		bDisconnect = false;
		bRecvRST = false;  // 디버깅용
		initTick = 0; // 디버깅용
		refCount = 0;
		refCount |= (1 << 31);
	}
	~Session()
	{
		delete sendLFQ;
	}


	//------------------------------------------------
	// 초기화 순서 중요
	// - AcquireSessionById에서 예외적 상황으로 증가시킨 RefCount를 감소하는 경우에 재할당된 세션을 끊어버릴 수 있음
	// - RefCount의 ReleaseFlag를 RefCount 증가 이후에 비활성화함으로써 해결
	//------------------------------------------------
	void InitSession(SOCKET _sock, ULONGLONG _id)
	{
		InterlockedIncrement(&refCount);
		recvOlp.olp.type = ERecv;
		memset(&recvOlp, 0, sizeof(WSAOVERLAPPED));
		sendOlp.olp.type = ESend;
		memset(&sendOlp, 0, sizeof(WSAOVERLAPPED));
		recvQ = nullptr;
		isSending = false;
		bDisconnect = false;
		bRecvRST = false; // 디버깅용
		initTick = timeGetTime(); // 디버깅용
		sessionId = _id;
		sock = _sock;
		InterlockedAnd((LONG*)&refCount, ~(1 << 31));
	}


public:
	SOCKET sock;
	ULONGLONG sessionId;
	SendOlp sendOlp;
	RecvOlp recvOlp;
	CLockFreeQueue<CPacket*>* sendLFQ;
	CPacket* recvQ;
	LONG isSending;
	LONG bDisconnect;
	ULONG refCount;
	ULONG bRecvRST;  // 디버깅용
	ULONG initTick;  // 디버깅용
};
