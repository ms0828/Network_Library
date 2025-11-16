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
	CloseHandle(jobEvent);
	CloseHandle(echoThreadHandle);
	delete jobQ;
}


bool CEchoServer::OnConnectionRequest(SOCKADDR_IN* requestAdr)
{
	return true;
}

void CEchoServer::OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId)
{
	CPacket* packet = CPacket::AllocSendPacket();
	packet->Clear();
	st_PacketHeader header;
	header.payloadLen = 8;
	ULONGLONG message = 0x7fffffffffffffff;
	packet->PutData((char*)&header, sizeof(header));
	packet->PutData((char*)&message, sizeof(ULONGLONG));
	SendPacket_Unicast(sessionId, packet);
	CPacket::ReleaseSendPacket(packet);
	return;
}

void CEchoServer::OnRelease(ULONGLONG sessionId)
{
	return;
}

void CEchoServer::OnMessage(ULONGLONG sessionId, USHORT packetType, CPacketViewer* message)
{
	//-------------------------------------------------------------
	// 에코(컨텐츠) 처리 스레드에게 작업 메시지를 생성 및 작업 큐에 인큐
	//-------------------------------------------------------------
	st_JobMessage jobMsg;
	jobMsg.sessionId = sessionId;
	jobMsg.message = message;
	
	AcquireSRWLockExclusive(&jogQLock);
	message->IncrementRefCount();
	int enqueueRet = jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
	ReleaseSRWLockExclusive(&jogQLock);
	if (enqueueRet == 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"JobQ is Full\n");
		exit(1);
	}
	SetEvent(jobEvent);
}


unsigned int CEchoServer::EchoThreadProc(void* arg)
{
	CEchoServer* core = static_cast<CEchoServer*>(arg);
	HANDLE hEvents[2] = { core->shutdownEvent, core->jobEvent };

	while (1)
	{
		if (core->jobQ->GetUseSize() == 0)
		{
			DWORD ret = WaitForMultipleObjects(2, hEvents, false, INFINITE);
			if (ret == WAIT_OBJECT_0)
				break;
		}

		st_JobMessage jobMsg;
		int dequeueRet = core->jobQ->Dequeue((char*)&jobMsg, sizeof(st_JobMessage));
		if (dequeueRet == 0)
			continue;
		
		CPacketViewer* message = jobMsg.message;

		st_PacketHeader header;
		header.payloadLen = message->GetDataSize();

		CPacket* packet = CPacket::AllocSendPacket();
		packet->PutData((char*)&header, sizeof(header));
		packet->PutData((char*)message->GetReadPtr(), sizeof(__int64));
		core->SendPacket_Unicast(jobMsg.sessionId, packet);
		CPacket::ReleaseSendPacket(packet);
		CPacketViewer::ReleasePacketViewer(jobMsg.message);
	}

	return 0;
}



void CEchoServer::OnMonitoring()
{
	printf("\n\n\n\n\n[CPacket]\n");
	printf("- sendPacketPool / Alloc Cnt = %d\n", CPacket::GetSendPacketAllocCount());
	printf("- sendPacketPool / ChunkPool Cnt = %d\n", CPacket::GetSendPacketChunkPoolCount());
	printf("- sendPacketPool / EmptyPool Cnt = %d\n", CPacket::GetSendPacketEmptyPoolCount());
	printf("\n");
	printf("- recvPacketPool / Alloc Cnt = %d\n", CPacket::GetRecvPacketAllocCount());
	printf("- recvPacketPool / ChunkPool Cnt = %d\n", CPacket::GetRecvPacketChunkPoolCount());
	printf("- recvPacketPool / EmptyPool Cnt = %d\n", CPacket::GetRecvPacketEmptyPoolCount());
	printf("\n");
	printf("- PacketViewerPool / Alloc Cnt = %d\n", CPacketViewer::GetPacketViewerAllocCount());
	printf("- PacketViewerPool / ChunkPool Cnt = %d\n", CPacketViewer::GetPacketViewerChunkPoolCount());
	printf("- PacketViewerPool / EmptyPool Cnt = %d\n", CPacketViewer::GetPacketViewerEmptyPoolCount());


	printf("\n");
	printf("[TPS]\n");
	printf("- accpetTPS = %d\n", GetAcceptTPS());
	printf("- sendMessageTPS = %d\n", GetSendMessageTPS());
	printf("- recvMessageTPS = %d\n\n\n\n\n", GetRecvMessageTPS());
	printf("------------------------------------------------------------------\n");
}


