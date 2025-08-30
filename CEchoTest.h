#pragma once
#include <Windows.h>

class CRingBuffer;
class CMyServer;
class CEcho;

struct st_EchoContext
{
	CMyServer* core;
	CEcho* echo;
};

struct st_JobMessage
{
	unsigned long long sessionId;
	__int64 echoData;
};

class CEcho
{
public:
	CEcho();
	~CEcho();

	static unsigned int EchoThreadProc(void* arg);

public:
	CRingBuffer* jobQ;
	SRWLOCK jogQLock;
	HANDLE jobEvent;
};


