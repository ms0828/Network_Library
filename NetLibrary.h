#pragma once
#include <WinSock2.h>
#include <Process.h>
#include <WS2tcpip.h>
#include <stack>
#include "CPacket.h"
#include "RingBuffer.h"
#include "ServerLog.h"
#include "Protocol.h"

#pragma comment (lib, "ws2_32")

enum EIOTYPE
{
	ERecv = 0,
	ESend = 1,
};
struct SessionOverlapped
{
	WSAOVERLAPPED overlapped;
	EIOTYPE type; // send인지 recv인지
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
			sendQ = new CRingBuffer();
			recvQ = new CRingBuffer();
			isSending = false;
			bDisconnect = false;
			ioCount = 0;
			//InitializeSRWLock(&lock);
		}
		~Session()
		{
			delete sendQ;
			delete recvQ;
		}

	public:
		SOCKET sock;
		ULONGLONG sessionId;
		SessionOverlapped sendOlp;
		SessionOverlapped recvOlp;
		CRingBuffer* sendQ;
		CRingBuffer* recvQ;
		//SRWLOCK lock;
		LONG isSending;
		LONG bDisconnect;
		unsigned int ioCount;
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
	bool SendPacket(ULONGLONG sessionId, CPacket* message);

	bool DisconnectSession(ULONGLONG sessionId);
	void ReleaseSession(Session* session);

	USHORT GetSessionArrIndex(ULONGLONG sessionId);


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


