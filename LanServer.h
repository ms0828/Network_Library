#pragma once
#include "NetLibrary.h"

class CLanServer
{
public:
	CLanServer();
	~CLanServer();

	//----------------------------------------------------------
	// CLanServer 기본 함수 선언부
	//----------------------------------------------------------
	bool Start(PCWSTR servIp, USHORT servPort, ULONG numOfWorkerThread, ULONG maxClientNum);
	void Stop();
	void RecvPost(Session* session);
	void SendPost(Session* session, bool bCallFromSendPacket);
	void SendPacket_Unicast(ULONGLONG sessionId, CPacket* packet);
	void SendPacket_Broadcast(ULONGLONG* sessionIdArr, ULONG numOfSession, CPacket* packet);
	bool DisconnectSession(ULONGLONG sessionId);
	void ReleaseSession(Session* session);
	USHORT GetSessionArrIndex(ULONGLONG sessionId);

	//-------------------------------------------------------
	// 세션Id로 세션을 검색 및 세션에 대한 참조권 확보
	// - 해당 세션을 검색 및 RefCount를 증가
	// - 이 함수로 찾은 세션은 RefCount를 감소하기 전까지 Release 되지 않음을 보장
	// 
	// [nullptr을 반환하는 경우]
	// - 찾으려는 세션이 없는 경우
	// - 찾으려는 세션이 이미 Release에 진입하였거나 Release 된 경우
	// - 찾으려는 세션이 이미 삭제되어 메모리가 재사용되어 찾으려는 세션과 다른 경우
	//-------------------------------------------------------
	Session* AcquireSessionById(ULONGLONG sessionId);

	//-------------------------------------------------------
	// 세션Id로 세션을 검색 [참조권을 획득하지 않는 함수]
	// 
	// [nullptr을 반환하는 경우]
	// - 찾으려는 세션이 없는 경우
	// - 찾으려는 세션이 이미 삭제되어 메모리가 재사용되어 찾으려는 세션과 다른 경우
	//-------------------------------------------------------
	Session* FindSessionById(ULONGLONG sessionId);

	//----------------------------------------------------------
	// 스레드 함수 선언부
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
	static unsigned int AcceptProc(void* arg);
	static unsigned int MonitoringThreadProc(void* arg);


	//----------------------------------------------------------
	// 이벤트 함수 선언부
	//----------------------------------------------------------
	virtual bool OnConnectionRequest(SOCKADDR_IN* requestAdr) = 0;
	virtual void OnAccept(SOCKADDR_IN* clnAdr, ULONGLONG sessionId) = 0;
	virtual void OnRelease(ULONGLONG sessionId) = 0;
	virtual void OnMessage(ULONGLONG sessionId, USHORT packetType, CPacketViewer* message) = 0;
	virtual void OnMonitoring() = 0;

	//----------------------------------------------------------
	// 모니터링 관련 함수 선언부
	//----------------------------------------------------------
	inline ULONG GetAcceptTPS()
	{
		ULONG ret = acceptTPS;
		acceptTPS = 0;
		return ret;
	}
	inline ULONG GetRecvMessageTPS()
	{
		ULONG ret = recvMessageTPS;
		recvMessageTPS = 0;
		return ret;
	}
	inline ULONG GetSendMessageTPS()
	{
		ULONG ret = sendMessageTPS;
		sendMessageTPS = 0;
		return ret;
	}

public:
	SOCKET listenSock;
	HANDLE hCp;

	//-------------------------------
	// 세션 관련
	//-------------------------------
	const static ULONGLONG sessionIdMask = (1ULL << 48) - 1;
	ULONGLONG sessionIdCnt;
	Session* sessionArr;
	CLockFreeStack<USHORT> indexStack;
	int numOfMaxSession;

	//-------------------------------
	// 스레드 관련
	//-------------------------------
	HANDLE* iocpWorkerHandleArr;
	HANDLE acceptThreadHandle;
	HANDLE monitoringThreadHandle;
	ULONG numOfWorkerThread;

protected:
	//-------------------------------
	// 모니터링 관련
	//-------------------------------
	ULONG acceptTPS;
	ULONG recvMessageTPS;
	ULONG sendMessageTPS;

	//-------------------------------
	// 종료 관련 이벤트
	//-------------------------------
	HANDLE shutdownEvent;
};
