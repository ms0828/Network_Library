#pragma once

#define PROFILE

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Process.h>
#include <stack>
#include "RingBuffer.h"
#include "Profiler.h"
#include "LockFreeQueue.h"
#include "Log.h"
#pragma comment (lib, "ws2_32")


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
		Session(SOCKET _sock, ULONGLONG _id)
		{
			sock = _sock;
			sessionId = _id;
			recvOlp.type = ERecv;
			sendOlp.type = ESend;
			//sendQ = new CRingBuffer();
			sendLFQ = new CLockFreeQueue<CPacket*>();
			isSending = false;
			bDisconnect = false;
			ioCount = 0;
			sendPacketCount = 0;
			recvQ = nullptr;
		}
		~Session()
		{
			delete sendLFQ;
		}

	public:
		SOCKET sock;
		ULONGLONG sessionId;
		SessionOverlapped sendOlp;
		SessionOverlapped recvOlp;
		//CRingBuffer* sendQ;
		CLockFreeQueue<CPacket*>* sendLFQ;
		CPacket* recvQ;			// 데이터 복사를 줄이기 위해 CPacket으로 직접 recv 받기
		LONG isSending;
		LONG bDisconnect;
		ULONG ioCount;
		

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
	void SendPost(Session* session);
	bool SendPacket(ULONGLONG sessionId, CPacket* packet);

	bool DisconnectSession(ULONGLONG sessionId);
	void ReleaseSession(Session* session);

	USHORT GetSessionArrIndex(ULONGLONG sessionId);


	//-------------------------------------------------------
	// 세션 배열에서 해당 세션 검색
	// 
	// [nullptr을 반환하는 경우]
	// - 찾으려는 세션이 이미 삭제된 경우
	// - 찾으려는 세션 메모리가 세션 삭제 후, 재사용되어 찾으려는 세션과 다른 경우 
	//-------------------------------------------------------
	Session* FindSession(ULONGLONG sessionId);

	//----------------------------------------------------------
	// 스레드 함수 선언부
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
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
	Session** sessionArr;

	std::stack<USHORT> indexStack;
	SRWLOCK indexStackLock;

	HANDLE* iocpWorkerHandleArr;
	HANDLE acceptThreadHandle;

	int sessionArrSize;
};

class CLanClient
{
public:



};


