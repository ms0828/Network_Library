#include "CEchoServer.h"
#include "CPacket.h"

using namespace std;

CEchoServer::CEchoServer()
{
	jobQ = new CRingBuffer(400000);
	jobEvent = CreateEvent(nullptr, false, false, nullptr);
	InitializeSRWLock(&jogQLock);

	echoThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, CEchoServer::EchoThreadProc, this, 0, nullptr);
}

CEchoServer::~CEchoServer()
{
	delete jobQ;
	CloseHandle(jobEvent);
	CloseHandle(echoThreadHandle);
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
	NetPacketProc_Echo(sessionId, echoData);
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




unsigned int CEchoServer::EchoThreadProc(void* arg)
{
	CEchoServer* core = static_cast<CEchoServer*>(arg);

	while (1)
	{
		if (core->jobQ->GetUseSize() == 0)
			WaitForSingleObject(core->jobEvent, INFINITE);

		st_JobMessage message;
		message.sessionId = 0;
		message.data = 0;
		int dequeueRet = core->jobQ->Dequeue((char*)&message, sizeof(st_JobMessage));
		if (dequeueRet == 0)
			continue;

		st_PacketHeader header;
		header.payloadLen = sizeof(message.data);


		CPacket* packet = CPacket::sendPacketPool.allocObject(dfSendPacketSize);
		packet->Clear();
		packet->PutData((char*)&header, sizeof(header));
		packet->PutData((char*)&message.data, sizeof(__int64));
		core->SendPacket(message.sessionId, packet);
	}

	return 0;
}

void CEchoServer::NetPacketProc_Echo(ULONGLONG sessionId, ULONGLONG echoData)
{
	st_JobMessage jobMsg;
	jobMsg.sessionId = sessionId;
	jobMsg.data = echoData;

	//-------------------------------------------------------------
	// 에코(컨텐츠) 처리 스레드에게 작업 메시지를 생성 및 작업 큐에 인큐
	//-------------------------------------------------------------
	AcquireSRWLockExclusive(&jogQLock);
	int enqueueRet = jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
	ReleaseSRWLockExclusive(&jogQLock);
	if (enqueueRet == 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"JobQ is Full\n");
		exit(1);
	}
	SetEvent(jobEvent);
}

