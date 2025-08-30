
#include "RingBuffer.h"
#include "CPacket.h"
#include "CMyServer.h"
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
		message.echoData = 0;
		int dequeueRet = echo->jobQ->Dequeue((char*)&message, sizeof(st_JobMessage));
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
