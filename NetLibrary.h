#pragma once

#define PROFILE

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Process.h>
#include <stack>
#include "RingBuffer.h"
#include "Profiler.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "Log.h"
#pragma comment (lib, "ws2_32")
#pragma comment(lib, "winmm.lib")

#define MAXSENDPACKETCOUNT 300

class CPacket;
class CRingBuffer;


enum EIOTYPE
{
	ERecv = 0,
	ESend = 1,
};
struct SessionOverlapped
{
	WSAOVERLAPPED overlapped;
	EIOTYPE type;
};

//-----------------------------------------------
// 패킷 헤더 구조
// (지금 에코 더미에 맞춰 간단한 패킷 헤더로 설계)
//-----------------------------------------------
struct st_PacketHeader
{
	unsigned short payloadLen;
};


class CLanServer
{
public:
	class Session
	{
	public:
		Session()
		{
			sock = INVALID_SOCKET;
			sessionId = 0;
			recvOlp.type = ERecv;
			sendOlp.type = ESend;
			sendLFQ = new CLockFreeQueue<CPacket*>();
			recvQ = nullptr;
			isSending = false;
			bDisconnect = false;
			bRecvRST = false;  // 디버깅용
			initTick = 0; // 디버깅용
			refCount = 0;
			sendPacketCount = 0;
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
			recvOlp.type = ERecv;
			memset(&recvOlp, 0, sizeof(WSAOVERLAPPED));
			sendOlp.type = ESend;
			memset(&sendOlp, 0, sizeof(WSAOVERLAPPED));
			recvQ = nullptr;
			isSending = false;
			bDisconnect = false;
			bRecvRST = false; // 디버깅용
			initTick = timeGetTime(); // 디버깅용
			sendPacketCount = 0;
			sessionId = _id;
			sock = _sock;
			InterlockedAnd((LONG*)&refCount, ~(1 << 31));
		}


	public:
		SOCKET sock;
		ULONGLONG sessionId;
		SessionOverlapped sendOlp;
		SessionOverlapped recvOlp;
		CLockFreeQueue<CPacket*>* sendLFQ;
		CPacket* recvQ;			// 데이터 복사를 줄이기 위해 CPacket으로 직접 recv 받기
		LONG isSending;
		LONG bDisconnect;
		ULONG refCount;
		ULONG bRecvRST;  // 디버깅용
		ULONG initTick;  // 디버깅용


		//--------------------------------------------------------
		// Zero Copy(TCP 송수신 관점이 아닌 데이터 복사 관점)를 위한 부분
		// - 반납할 CPacket 포인터 관리
		//--------------------------------------------------------
		CPacket* freePacket[MAXSENDPACKETCOUNT];
		ULONG sendPacketCount;
		
	};


public:
	CLanServer();
	~CLanServer();
	
	//----------------------------------------------------------
	// CLanServer 기본 함수 선언부
	//----------------------------------------------------------
	bool Start(PCWSTR servIp, USHORT servPort, ULONG numOfWorkerThread, ULONG maxClientNum);
	void Stop();

	void RecvPost(Session* session);
	void SendPost(Session* session, bool bCallFromSendPacket);
	bool SendPacket(ULONGLONG sessionId, CPacket* packet);

	bool DisconnectSession(ULONGLONG sessionId);

	void ReleaseSession(Session* session);

	USHORT GetSessionArrIndex(ULONGLONG sessionId);


	//-------------------------------------------------------
	// 세션Id로 세션을 검색 및 세션에 대한 참조권 확보
	// - 해당 세션을 검색 및 RefCount를 증가
	// - 이 함수로 찾은 세션은 RefCount를 감소하기 전까지 세션이 바뀌지 않음을 보장할 수 있다.
	// 
	// [nullptr을 반환하는 경우]
	// - 찾으려는 세션이 없는 경우
	// - 찾으려는 세션이 이미 Release에 진입하였거나 Release 된 경우
	// - 찾으려는 세션이 이미 삭제되어 메모리가 재사용되어 찾으려는 세션과 다른 경우
	//-------------------------------------------------------
	Session* AcquireSessionById(ULONGLONG sessionId);
	

	//-------------------------------------------------------
	// 세션Id로 세션을 검색 [참조권을 획득하지 않는 함수]
	// 
	// [nullptr을 반환하는 경우]
	// - 찾으려는 세션이 없는 경우
	// - 찾으려는 세션이 이미 삭제되어 메모리가 재사용되어 찾으려는 세션과 다른 경우
	//-------------------------------------------------------
	Session* FindSessionById(ULONGLONG sessionId);



	//----------------------------------------------------------
	// 스레드 함수 선언부
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
	static unsigned int DebugThreadProc(void* arg); // 디버깅용 스레드
	static unsigned int AcceptProc(void* arg);
	


	//----------------------------------------------------------
	// 이벤트 함수 선언부
	//----------------------------------------------------------
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr) = 0;
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId) = 0;
	virtual void OnRelease(ULONGLONG sessionId) = 0;
	virtual void OnMessage(ULONGLONG sessionId, CPacket* message) = 0;


private:
	SOCKET listenSock;
	HANDLE hCp;
	ULONG sessionIdCnt;
	Session* sessionArr;
	CLockFreeStack<USHORT> indexStack;

	HANDLE* iocpWorkerHandleArr;
	HANDLE acceptThreadHandle;

	int numOfMaxSession;
};

class CLanClient
{
public:



};


