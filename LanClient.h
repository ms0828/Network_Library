#pragma once
#include "NetLibrary.h"


//-------------------------------------------------------------------------
// 연결 끊김과 재접속을 시뮬레이션할 더미 제작을 대비하며 LanClient를 제작
// - 외부 스레드에서는 현재 접근하려는 Session이 LanClient의 Session과 다를 수 있으므로, 세션 Id 기반으로 검색 필요  
//-------------------------------------------------------------------------
class CLanClient
{
public:
	CLanClient(ULONG numOfWorker);
	~CLanClient();

	bool Connect(PCWSTR servIp, USHORT servPort);
	bool Disconnect(ULONGLONG sessionId);
	void ReleaseSession(Session* session);
	void RecvPost(Session* session);
	void SendPost(Session* session, bool bCallFromSendPacket);
	void SendPacket(ULONGLONG sessionId, CPacket* packet);
	Session* AcquireSessionById(ULONGLONG sessionId);
	virtual void OnConnect() = 0;
	virtual void OnDisconnect() = 0;
	virtual void OnMessage(USHORT packetType, CPacketViewer* message) = 0;
	virtual void OnMonitoring() = 0;

	//----------------------------------------------------------
	// 스레드 함수 선언부
	//----------------------------------------------------------
	static unsigned int IOCPWorkerProc(void* arg);
	static unsigned int MonitoringThreadProc(void* arg);


	//----------------------------------------------------------
	// 모니터링 관련 함수 선언부
	//----------------------------------------------------------
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


private:
	HANDLE hCp;
	Session* session;
	ULONGLONG sessionIdCnt;

	//-------------------------------
	// 스레드 관련
	//-------------------------------
	HANDLE* iocpWorkerHandleArr;
	HANDLE monitoringThreadHandle;
	ULONG numOfWorkerThread;


protected:
	//-------------------------------
	// 모니터링 관련
	//-------------------------------
	ULONG recvMessageTPS;
	ULONG sendMessageTPS;

	//-------------------------------
	// 종료 관련 이벤트
	//-------------------------------
	HANDLE shutdownEvent;
};


