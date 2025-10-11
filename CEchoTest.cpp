#include "CMyServer.h"
#include "RingBuffer.h"
#include "CPacket.h"
#include "CEchoTest.h"

CEcho::CEcho()
{
	jobQ = new CRingBuffer(400000);
	jobEvent = CreateEvent(nullptr, false, false, nullptr);
	InitializeSRWLock(&jogQLock);
}
CEcho::~CEcho()
{
	delete jobQ;
}



unsigned int CEcho::EchoThreadProc(void* arg)
{
	st_EchoContext* context = static_cast<st_EchoContext*>(arg);
	CMyServer* core = context->core;
	CEcho* echo = context->echo;
	while (1)
	{
		if (echo->jobQ->GetUseSize() == 0)
			WaitForSingleObject(echo->jobEvent, INFINITE);

		st_JobMessage message;
		message.sessionId = 0;
		message.data = 0;
		int dequeueRet = echo->jobQ->Dequeue((char*)&message, sizeof(st_JobMessage));
		if (dequeueRet == 0)
			continue;

		st_PacketHeader header;
		header.payloadLen = sizeof(message.data);
		

		AcquireSRWLockExclusive(&CPacket::sendCPacketPoolLock);
		CPacket* packet = CPacket::sendCPacketPool.allocObject();
		ReleaseSRWLockExclusive(&CPacket::sendCPacketPoolLock);
		packet->Clear();
		packet->PutData((char*)&header, sizeof(header));
		packet->PutData((char*)&message.data, sizeof(__int64));
		core->SendPacket(message.sessionId, packet);
	}

	return 0;
}

void CEcho::NetPacketProc_Echo(ULONGLONG sessionId, ULONGLONG echoData)
{
	st_JobMessage jobMsg;
	jobMsg.sessionId = sessionId;
	jobMsg.data = echoData;

	//-------------------------------------------------------------
	// ����(������) ó�� �����忡�� �۾� �޽����� ���� �� �۾� ť�� ��ť
	// - Enqueue �� �� �� ������ �ʿ�!
	//-------------------------------------------------------------

	AcquireSRWLockExclusive(&jogQLock);
	int enqueueRet = jobQ->Enqueue((char*)&jobMsg, sizeof(jobMsg));
	ReleaseSRWLockExclusive(&jogQLock);
	if (enqueueRet == 0)
	{
		std::cout << "JobQ�� �� á���ϴ�.\n";
		exit(1);
	}
	SetEvent(jobEvent);
}
