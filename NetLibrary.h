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
// ��Ŷ ��� ����
// (���� ���� ���̿� ���� ������ ��Ŷ ����� ����)
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
			bRecvRST = false;  // ������
			initTick = 0; // ������
			refCount = 0;
			sendPacketCount = 0;
		}
		~Session()
		{
			delete sendLFQ;
		}


		//------------------------------------------------
		// �ʱ�ȭ ���� �߿�
		// - AcquireSessionById���� ������ ��Ȳ���� ������Ų RefCount�� �����ϴ� ��쿡 ���Ҵ�� ������ ������� �� ����
		// - RefCount�� ReleaseFlag�� RefCount ���� ���Ŀ� ��Ȱ��ȭ�����ν� �ذ�
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
			bRecvRST = false; // ������
			initTick = timeGetTime(); // ������
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
		CPacket* recvQ;			// ������ ���縦 ���̱� ���� CPacket���� ���� recv �ޱ�
		LONG isSending;
		LONG bDisconnect;
		ULONG refCount;
		ULONG bRecvRST;  // ������
		ULONG initTick;  // ������


		//--------------------------------------------------------
		// Zero Copy(TCP �ۼ��� ������ �ƴ� ������ ���� ����)�� ���� �κ�
		// - �ݳ��� CPacket ������ ����
		//--------------------------------------------------------
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
	void SendPost(Session* session, bool bCallFromSendPacket);
	bool SendPacket(ULONGLONG sessionId, CPacket* packet);

	bool DisconnectSession(ULONGLONG sessionId);

	void ReleaseSession(Session* session);

	USHORT GetSessionArrIndex(ULONGLONG sessionId);


	//-------------------------------------------------------
	// ����Id�� ������ �˻� �� ���ǿ� ���� ������ Ȯ��
	// - �ش� ������ �˻� �� RefCount�� ����
	// - �� �Լ��� ã�� ������ RefCount�� �����ϱ� ������ ������ �ٲ��� ������ ������ �� �ִ�.
	// 
	// [nullptr�� ��ȯ�ϴ� ���]
	// - ã������ ������ ���� ���
	// - ã������ ������ �̹� Release�� �����Ͽ��ų� Release �� ���
	// - ã������ ������ �̹� �����Ǿ� �޸𸮰� ����Ǿ� ã������ ���ǰ� �ٸ� ���
	//-------------------------------------------------------
	Session* AcquireSessionById(ULONGLONG sessionId);
	

	//-------------------------------------------------------
	// ����Id�� ������ �˻� [�������� ȹ������ �ʴ� �Լ�]
	// 
	// [nullptr�� ��ȯ�ϴ� ���]
	// - ã������ ������ ���� ���
	// - ã������ ������ �̹� �����Ǿ� �޸𸮰� ����Ǿ� ã������ ���ǰ� �ٸ� ���
	//-------------------------------------------------------
	Session* FindSessionById(ULONGLONG sessionId);



	//----------------------------------------------------------
	// ������ �Լ� �����
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
	static unsigned int DebugThreadProc(void* arg); // ������ ������
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


