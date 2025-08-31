#pragma once
#include "NetLibrary.h"
#include "CEchoTest.h"


class CMyServer : public CLanServer
{
public:
	CMyServer();
	~CMyServer();

public:
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr);
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId);
	virtual void OnRelease(ULONGLONG sessionId);
	virtual void OnMessage(ULONGLONG sessionId, CPacket* message);


private:
	CEcho* echo;
	st_EchoContext* echoContext;
	HANDLE echoThreadHandle;
};
