#pragma once
#include "NetLibrary.h"
#include "CEchoTest.h"


class CEchoServer : public CLanServer
{
public:
	CEchoServer();
	~CEchoServer();

public:
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr);
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId);
	virtual void OnRelease(ULONGLONG sessionId);
	virtual void OnMessage(ULONGLONG sessionId, CPacket* message);
	virtual void OnMonitoring();

private:
	CEcho* echo;
	st_EchoContext* echoContext;
	HANDLE echoThreadHandle;
};
