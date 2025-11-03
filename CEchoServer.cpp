#include "CEchoServer.h"
#include "CPacket.h"

using namespace std;

CEchoServer::CEchoServer()
{
	echo = new CEcho;
	echoContext = new st_EchoContext;
	echoContext->core = this;
	echoContext->echo = echo;
	echoThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, CEcho::EchoThreadProc, echoContext, 0, nullptr);
}

CEchoServer::~CEchoServer()
{
	delete echo;
	delete echoContext;
}


bool CEchoServer::OnConnectionRequest(SOCKADDR_IN* requestAdr)
{
	return true;
}

void CEchoServer::OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId)
{
	CPacket* packet = CPacket::sendPacketPool.allocObject(dfSendPacketSize);
	packet->Clear();
	st_PacketHeader header;
	header.payloadLen = 8;
	ULONGLONG message = 0x7fffffffffffffff;
	packet->PutData((char*)&header, sizeof(header));
	packet->PutData((char*)&message, sizeof(ULONGLONG));
	SendPacket(sessionId, packet);

	return;
}

void CEchoServer::OnRelease(ULONGLONG sessionId)
{
	return;
}

void CEchoServer::OnMessage(ULONGLONG sessionId, CPacket* message)
{
	//------------------------------------------------------------
	// - 간단한 에코 서버이므로, 메시지를 헤더 타입으로 분류하여 메시지 종류별 처리하는 과정은 생략
	// - 현재 에코 더미 자체가 헤더 안에 메시지 타입을 기재하고 있지 않음
	//------------------------------------------------------------
	ULONGLONG echoData;
	*message >> echoData;
	echo->NetPacketProc_Echo(sessionId, echoData);
}

void CEchoServer::OnMonitoring()
{
	printf("\n\n\n\n\n[CPacket]\n");
	printf("- sendPacketPool / Chunk Pool Cnt = %d\n", CPacket::sendPacketPool.chunkPool.GetSize());
	printf("- sendPacketPool / Empty Pool Cnt = %d\n", CPacket::sendPacketPool.emptyChunkPool.GetSize());
	printf("- recvPacketPool / Chunk Pool Cnt = %d\n", CPacket::recvPacketPool.chunkPool.GetSize());
	printf("- recvPacketPool / Empty Pool Cnt = %d\n", CPacket::recvPacketPool.emptyChunkPool.GetSize());
	printf("\n");
	printf("[TPS]\n");
	printf("- accpetTPS = %d\n", GetAcceptTPS());
	printf("- sendMessageTPS = %d\n", GetSendMessageTPS());
	printf("- recvMessageTPS = %d\n\n\n\n\n", GetRecvMessageTPS());
	printf("------------------------------------------------------------------\n");
}



