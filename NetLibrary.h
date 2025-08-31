#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Process.h>
#include <stack>
#include "RingBuffer.h"
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

struct st_Header
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
			sendQ = new CRingBuffer();
			recvQ = new CRingBuffer();
			isSending = false;
			bDisconnect = false;
			ioCount = 0;
			sendPacketCount = 0;
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
		LONG isSending;
		LONG bDisconnect;
		ULONG ioCount;
		CPacket* freePacket[MAXSENDPACKETCOUNT];
		ULONG sendPacketCount;
	};


public:
	CLanServer();
	~CLanServer();
	
	//----------------------------------------------------------
	// CLanServer �⺻ �Լ� �����
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
	// ���� �迭���� �ش� ���� �˻�
	// 
	// [nullptr�� ��ȯ�ϴ� ���]
	// - ã������ ������ �̹� ������ ���
	// - ã������ ���� �޸𸮰� ���� ���� ��, ����Ǿ� ã������ ���ǰ� �ٸ� ��� 
	//-------------------------------------------------------
	Session* FindSession(ULONGLONG sessionId);

	//----------------------------------------------------------
	// ������ �Լ� �����
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
	static unsigned int AcceptProc(void* arg);



	//----------------------------------------------------------
	// �̺�Ʈ �Լ� �����
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


