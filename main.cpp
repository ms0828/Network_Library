#include <iostream>
#include <conio.h>
#include "NetLibrary.h"

using namespace std;

struct st_JobMessage
{
	ULONGLONG sessionId;
	__int64 echoData;
};

class MyServer : public CLanServer
{
public:
	MyServer()
	{
		jobQ = new CRingBuffer(400000);
		echoThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, EchoThreadProc, this, 0, nullptr);
	};
	~MyServer()
	{
		delete jobQ;
	};

private:
	CRingBuffer* jobQ;
	SRWLOCK jogQLock;
	HANDLE jobEvent;
	HANDLE echoThreadHandle;
public:
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr)
	{
		return true;
	}
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId)
	{
		return;
	}
	virtual void OnRelease(ULONGLONG sessionId)
	{
		return;
	}
	virtual void OnMessage(ULONGLONG sessionId, CPacket* message)
	{
		//------------------------------------------------------------
		// - 간단한 에코 서버이므로, 메시지를 헤더 타입으로 분류하여 메시지 종류별 처리하는 과정은 생략
		// - 현재 에코 더미 자체가 헤더 안에 메시지 타입을 기재하고 있지 않음
		//------------------------------------------------------------
		unsigned short payloadLen;
		__int64 echoData;
		*message >> payloadLen;
		*message >> echoData;

		//-------------------------------------------------------------
		// 에코(컨텐츠) 처리 스레드에게 작업 메시지를 생성 및 작업 큐에 인큐
		// - Enqueue 할 때 락 무조건 필요!
		//-------------------------------------------------------------
		st_JobMessage jobMsg;
		jobMsg.sessionId = sessionId;
		jobMsg.echoData = echoData;
		AcquireSRWLockExclusive(&jogQLock);
		int enqueueRet = jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
		ReleaseSRWLockExclusive(&jogQLock);
		if (enqueueRet == 0)
		{
			cout << "JobQ가 다 찼습니다.\n";
			exit(1);
		}
		SetEvent(jobEvent);
	}


	static unsigned int EchoThreadProc(void* arg)
	{
		MyServer* core = static_cast<MyServer*>(arg);
		while (1)
		{
			if (core->jobQ->GetUseSize() == 0)
				WaitForSingleObject(core->jobEvent, INFINITE);

			st_JobMessage message;
			message.sessionId = 0;
			message.echoData = 0;
			int dequeueRet = core->jobQ->Dequeue((char*)&message, sizeof(st_JobMessage));
			if (dequeueRet == 0)
				continue;

			st_Header header;
			header.payloadLen = 8;
			CPacket packet;
			packet.PutData((char*)&header, sizeof(header));
			packet.PutData((char*)&message.echoData, sizeof(__int64));
			core->SendPacket(message.sessionId, &packet);
		}

		return 0;
	}
};


int main()
{
	MyServer server;
	server.Start(L"0.0.0.0", 6000, 5, 5000);
	
	
	while (1)
	{
		if (_kbhit())
		{
			WCHAR ControlKey = _getwch();

			if (ControlKey == L's' || ControlKey == L'S')
			{

			}
		}
	}

}