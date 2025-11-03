#pragma once
#include <Windows.h>

class CRingBuffer;
class CEchoServer;
class CPacket;
class CEcho;

struct st_JobMessage
{
	ULONGLONG sessionId;
	ULONGLONG data;
};

struct st_EchoContext
{
	CEchoServer* core;
	CEcho* echo;
};


class CEcho
{
public:
	CEcho();
	~CEcho();

	static unsigned int EchoThreadProc(void* arg);

	void NetPacketProc_Echo(ULONGLONG sessionId, ULONGLONG echoData);
	

public:
	CRingBuffer* jobQ;
	SRWLOCK jogQLock;
	HANDLE jobEvent;
};


