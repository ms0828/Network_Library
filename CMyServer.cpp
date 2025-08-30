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
	// - ������ ���� �����̹Ƿ�, �޽����� ��� Ÿ������ �з��Ͽ� �޽��� ������ ó���ϴ� ������ ����
	// - ���� ���� ���� ��ü�� ��� �ȿ� �޽��� Ÿ���� �����ϰ� ���� ����
	//------------------------------------------------------------
	unsigned short payloadLen;
	__int64 echoData;
	*message >> payloadLen;
	*message >> echoData;

	//-------------------------------------------------------------
	// ����(������) ó�� �����忡�� �۾� �޽����� ���� �� �۾� ť�� ��ť
	// - Enqueue �� �� �� ������ �ʿ�!
	//-------------------------------------------------------------
	st_JobMessage jobMsg;
	jobMsg.sessionId = sessionId;
	jobMsg.echoData = echoData;
	AcquireSRWLockExclusive(&echo->jogQLock);
	int enqueueRet = echo->jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
	ReleaseSRWLockExclusive(&echo->jogQLock);
	if (enqueueRet == 0)
	{
		cout << "JobQ�� �� á���ϴ�.\n";
		exit(1);
	}
	SetEvent(echo->jobEvent);
}
