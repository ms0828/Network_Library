#pragma once
#include "NetLibrary.h"

struct st_JobMessage
{
	ULONGLONG sessionId;
	ULONGLONG data;
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
	virtual void OnMessage(ULONGLONG sessionId, CPacket* message);
	virtual void OnMonitoring();


	//---------------------------------------------------------------
	// 俊内 牧刨明 包访
	//---------------------------------------------------------------
	static unsigned int EchoThreadProc(void* arg);
	void NetPacketProc_Echo(ULONGLONG sessionId, ULONGLONG echoData);


private:
	//---------------------------------------------------------------
	// 俊内 牧刨明 包访
	//---------------------------------------------------------------
	HANDLE echoThreadHandle;
	CRingBuffer* jobQ;
	SRWLOCK jogQLock;
	HANDLE jobEvent;
};
