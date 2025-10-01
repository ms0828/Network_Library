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
	ULONGLONG echoData;
	*message >> echoData;
	echo->NetPacketProc_Echo(sessionId, echoData);

}
