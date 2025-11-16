#pragma once
#include "LanServer.h"

struct st_JobMessage
{
	ULONGLONG sessionId;
	CPacketViewer* message;
};

class CEchoServer : public CLanServer
{
public:
	CEchoServer();
	~CEchoServer();

public:
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr);
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId);
	virtual void OnRelease(ULONGLONG sessionId);
	virtual void OnMessage(ULONGLONG sessionId, USHORT packetType, CPacketViewer* message);
	virtual void OnMonitoring();

	//---------------------------------------------------------------
	// 俊内 牧刨明 包访
	//---------------------------------------------------------------
	static unsigned int EchoThreadProc(void* arg);


private:
	//---------------------------------------------------------------
	// 俊内 牧刨明 包访
	//---------------------------------------------------------------
	HANDLE echoThreadHandle;
	CRingBuffer* jobQ;
	SRWLOCK jogQLock;
	HANDLE jobEvent;
};
