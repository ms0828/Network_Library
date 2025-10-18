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
	CPacket* packet = CPacket::sendCPacketPool.allocObject();
	packet->Clear();
	st_PacketHeader header;
	header.payloadLen = 8;
	ULONGLONG message = 0x7fffffffffffffff;
	packet->PutData((char*)&header, sizeof(header));
	packet->PutData((char*)&message, sizeof(ULONGLONG));
	if (SendPacket(sessionId, packet) == false)
		CPacket::sendCPacketPool.freeObject(packet);

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
	ULONGLONG echoData;
	*message >> echoData;
	echo->NetPacketProc_Echo(sessionId, echoData);
}
