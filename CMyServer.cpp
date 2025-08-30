#include "CMyServer.h"
#include "CPacket.h"

using namespace std;

CMyServer::CMyServer()
{
	echo = new CEcho;
	echoContext = new st_EchoContext;
	echoContext->core = this;
	echoContext->echo = echo;
	echoThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, CEcho::EchoThreadProc, echoContext, 0, nullptr);
}

CMyServer::~CMyServer()
{
	delete echo;
	delete echoContext;
}


bool CMyServer::OnConnectionRequest(SOCKADDR_IN* requestAdr)
{
	return true;
}
void CMyServer::OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId)
{
	return;
}

void CMyServer::OnRelease(ULONGLONG sessionId)
{
	return;
}

void CMyServer::OnMessage(ULONGLONG sessionId, CPacket* message)
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
	AcquireSRWLockExclusive(&echo->jogQLock);
	int enqueueRet = echo->jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
	ReleaseSRWLockExclusive(&echo->jogQLock);
	if (enqueueRet == 0)
	{
		cout << "JobQ가 다 찼습니다.\n";
		exit(1);
	}
	SetEvent(echo->jobEvent);
}
