#include "LanClient.h"

CLanClient::CLanClient(ULONG numOfWorker)
{
	hCp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	shutdownEvent = CreateEvent(nullptr, true, false, nullptr);
	session = new Session();
	sessionIdCnt = 1;
	recvMessageTPS = 0;
	sendMessageTPS = 0;

	numOfWorkerThread = numOfWorker;
	monitoringThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, MonitoringThreadProc, this, 0, nullptr);
	iocpWorkerHandleArr = new HANDLE[numOfWorkerThread];
	for (int i = 0; i < numOfWorkerThread; i++)
		iocpWorkerHandleArr[i] = (HANDLE)_beginthreadex(nullptr, 0, IOCPWorkerProc, this, 0, nullptr);


	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"WSAStartUp Fail");
		return;
	}

}

CLanClient::~CLanClient()
{
	delete[] iocpWorkerHandleArr;
	delete session;
	WSACleanup();
}

bool CLanClient::Connect(PCWSTR servIp, USHORT servPort)
{
	if (session->sock != INVALID_SOCKET)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"[Connect] connect Fail - Already Session Connected");
		return false;
	}

	SOCKET clnSock = socket(AF_INET, SOCK_STREAM, 0);
	if (clnSock == INVALID_SOCKET)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"[Connect] connect Fail - Create Session Sock Fail / errorCode = %d", WSAGetLastError());
		return false;
	}

	//----------------------------------------------------------
	// LINGER 옵션 설정 및 송신 시 I/O Pending 유도
	//----------------------------------------------------------
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	if (setsockopt(clnSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"[Connect] setsockopt Fail - LINGER Option Fail / errorCode = %d", WSAGetLastError());
		return false;
	}
	int optval = 0;
	if (setsockopt(clnSock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval)) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"[Connect] setsockopt Fail - Send Buf Size Setting / errorCode = %d", WSAGetLastError());
		return false;
	}


	//---------------------------------------------
	// connect
	//---------------------------------------------
	SOCKADDR_IN servAdr;
	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	InetPton(AF_INET, servIp, &servAdr.sin_addr);
	servAdr.sin_port = htons(servPort);
	servAdr.sin_addr.s_addr;
	if (connect(clnSock, (SOCKADDR*)&servAdr, sizeof(servAdr)) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"[Connect] connect Fail - connect return SOCKET_ERROR / errorCode = %d", WSAGetLastError());
		return false;
	}

	//------------------------------------------------------
	// connect 성공 시 세션 초기화
	//------------------------------------------------------
	session->InitSession(clnSock, ++sessionIdCnt);
	CreateIoCompletionPort((HANDLE)session->sock, hCp, (ULONG_PTR)session->sessionId, 0);

	OnConnect();

	RecvPost(session);

	return true;
}

bool CLanClient::Disconnect(ULONGLONG sessionId)
{
	//------------------------------------------------------------
	// 세션 검색 및 세션 확보(Ref Count 증가)
	// - Disconnect flag를 활성화 하기 위해 세션 참조권 확보 필요
	//		- 엉뚱한 세션에 Disconnect flag 활성화 방지
	//------------------------------------------------------------
	Session* session = AcquireSessionById(sessionId);
	if (session == nullptr)
		return false;

	//-------------------------------------------------------
	// 해당 세션의 Disconnect flag 활성화
	//-------------------------------------------------------
	InterlockedExchange(&session->bDisconnect, 1);


	//--------------------------------------------------------
	// - 상대방이 recv IO PENDING이 걸려있다면, disconnect flag를 올려두어도 완료통지가 오지 않아 refCount가 1로 남아있게 된다.
	// - 어떻게 이를 끊을 것인가?
	// - shutdown을 써서 미완료 통지의 IO를 중단시키는 방법을 생각해봤으나, graceful shutdown을 진입하는 것이 마음에 들지 않는다.
	// - shutdown 대신 CancelIoEx를 이용하여 IO 요청을 중단시키는 방법을 선택
	//--------------------------------------------------------
	CancelIoEx((HANDLE)session->sock, nullptr);


	//-------------------------------------------------------
	// 확보한 세션의 Ref Count 복구
	//-------------------------------------------------------
	ULONG refCount = InterlockedDecrement(&session->refCount);
	_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / DisconnectSession / Decrement RefCount = %d)! \n", session->sessionId, refCount);
	if (refCount == 0)
		ReleaseSession(session);

	return true;
}

void CLanClient::ReleaseSession(Session* session)
{
	//------------------------------------------------------------------------
	// RefCount의 최상위 비트를 Release를 나타내는 Flag를 두어, Release 진행 시 세션 접근을 막는다.
	// - RefCount가 0이라면 ReleaseFlag 활성화 후 Release 수행
	// - RefCount가 0이 아니라면 Release는 취소
	//------------------------------------------------------------------------
	ULONG releaseFlag = (1 << 31);
	if (InterlockedCompareExchange(&session->refCount, releaseFlag, 0) != 0)
		return;

	if (session->bRecvRST == false)
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / ReleaseSession With Not RST\n", session->sessionId);
	else
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / ReleaseSession\n", session->sessionId);


	//-----------------------------------------------------------------------
	// 해당 세션에서 반납하지 못한 CPacket 반환
	// - Dequeue되어 전송하지 못한 SendLFQ에 존재하는 CPacket
	// - 반납되지 못한 Session의 freePacket에 존재하는 CPacket
	// - WSARecv 호출에 실패하여 반납되지 못한 session->recvQ에 저장된 CPacket
	//-----------------------------------------------------------------------
	while (1)
	{
		CPacket* packet = nullptr;
		if (session->sendLFQ->Dequeue(packet) == false)
			break;
		CPacket::ReleaseSendPacket(packet);
	};
	for (int i = 0; i < session->sendOlp.sendCount; i++)
		CPacket::ReleaseSendPacket(session->sendOlp.freePackets[i]);

	if (session->recvQ != nullptr)
		CPacket::ReleaseRecvPacket(session->recvQ);

	
	//------------------------------------------------------------------------
	// 해당 세션 메모리 초기화
	//------------------------------------------------------------------------
	closesocket(session->sock);
	session->sock = INVALID_SOCKET;

	OnDisconnect();

	return;
}

void CLanClient::RecvPost(Session* session)
{
	PRO_BEGIN("RecvPost");

	//------------------------------------------------------------------
	// RecvPost가 취소 되는 상황
	// 1. 해당 세션의 Disconnect 플래그가 활성화된 경우
	// 2. 해당 세션의 RecvQ가 꽉 찬 경우
	//		- 이 경우는 해당 세션의 Disconnect 플래그 활성화하여 세션 종료 유도
	//-------------------------------------------------------------------
	if (session->bDisconnect)
	{
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / RecvPost Cancle - bDisconnect / Decrement RefCount = %d \n", session->sessionId, refCount);
		if (refCount == 0)
			ReleaseSession(session);
		return;
	}

	_LOG(dfLOG_LEVEL_DEBUG, L"------------AsyncRecv  session id : %016llx------------\n", session->sessionId);

	//------------------------------------------------------------------
	// 1. 새로운 recvQ(CPacket)를 CPacket 풀에서 할당 및 이전에 사용했던 recvQ(CPacket) 반납
	//  - 이전 recvQ(CPacket)에 미완성된 데이터가 있다면 현재 할당한 recvQ(CPacket)로 옮기기
	// 2. 해당 session에 할당 받은 recvQ(CPacket) 포인터 등록
	//-------------------------------------------------------------------
	CPacket* newRecvQ = CPacket::AllocRecvPacket();
	if (session->recvQ != nullptr)
	{
		if (session->recvQ->GetDataSize() > 0)
			int putRet = newRecvQ->PutData(session->recvQ->GetReadPtr(), session->recvQ->GetDataSize());
		CPacket::ReleaseRecvPacket(session->recvQ);
	}
	session->recvQ = newRecvQ;


	//------------------------------------------------------------------
	// WSARecv 호출
	//-------------------------------------------------------------------
	//_LOG(dfLOG_LEVEL_ERROR, L"Increment RefCount - RecvPost\n");
	memset(&session->recvOlp, 0, sizeof(WSAOVERLAPPED));
	WSABUF wsaRecvBuf;
	wsaRecvBuf.buf = newRecvQ->GetBufferPtr();
	wsaRecvBuf.len = newRecvQ->GetBufferSize() - newRecvQ->GetDataSize();
	if (wsaRecvBuf.len == 0)
	{
		// recvQ가 가득찬 상황은 설계된 메시지 크기를 초과한 메시지가 온 것
		// - 연결 종료로 대처
		InterlockedExchange(&session->bDisconnect, true);
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_SYSTEM, L"session id : %016llx / recvQ is Full / Decrement RefCount = %d \n", session->sessionId, refCount);
		if (refCount == 0)
			ReleaseSession(session);
		return;
	}
	DWORD flags = 0;
	int recvRet = WSARecv(session->sock, &wsaRecvBuf, 1, nullptr, &flags, (WSAOVERLAPPED*)&session->recvOlp, nullptr);
	if (recvRet == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L" RECV (FAST I/O) \n");
	}
	else if (recvRet == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error == WSA_IO_PENDING)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L"RECV IO PENDING\n");
		}
		else
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / RecvError / errorCode = %d / Decrement RefCount = %d)! \n", session->sessionId, error, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
	}

	PRO_END("RecvPost");
	return;
}

void CLanClient::SendPost(Session* session, bool bCallFromSendPacket)
{
	PRO_BEGIN("SendPost");
	//--------------------------------------------------------
	// SendPost가 취소 되는 상황
	// 1. 현재 Send가 진행 중
	// 2. 해당 세션의 Disconnect 플래그가 활성화된 경우
	// 3. 해당 세션의 SendQ가 비어있는 경우
	//--------------------------------------------------------
	if (InterlockedCompareExchange(&session->isSending, true, false) == true || session->bDisconnect)
	{
		if (bCallFromSendPacket)
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost Cancle / Decrement RefCount = %d)! \n", session->sessionId, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
		return;
	}


	_LOG(dfLOG_LEVEL_DEBUG, L"------------AsyncSend  session id : %016llx------------\n", session->sessionId);


	//--------------------------------------------------------
	// SendQ에 대한 WSABUF 세팅
	// - SendQ에 담긴 패킷 수가 0이라면 SendPost 취소
	// - SendQ에 담긴 패킷 수가 최대 보낼 수 있는 패킷 수 보다 많다면 비정상적인 상황이므로 해당 연결 끊기
	//--------------------------------------------------------
	WSABUF wsaBufArr[dfMaxSendPacketCount];
	int sendQSize = session->sendLFQ->size;
	if (sendQSize == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"id : %016llx  / LFQ size = 0\n", session->sessionId);
		InterlockedExchange(&session->isSending, false);

		//------------------------------------------------------------------------------------------
		// [ 예외 처리 ]
		// size를 0으로 봤으나, 이 시점에 소유권으로 인해 다른 SendPacket에서 SendPost가 실패한다면 마지막 Enqueue된 패킷은 전송되지 못한다.
		// => size가 0인 시점에 소유권 포기 이후, 한 번더 size를 체크하여 재시도를 수행한다.
		//------------------------------------------------------------------------------------------
		if (session->sendLFQ->size > 0)
			SendPost(session, false);

		if (bCallFromSendPacket)
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost(Call From SendPacket) LFQ size = 0 / Decrement RefCount = %d)! \n", session->sessionId, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
		return;
	}
	if (sendQSize > dfMaxSendPacketCount)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"packetCount overflow / id : %016llx------------\n", session->sessionId);
		InterlockedExchange(&session->bDisconnect, true);

		if (bCallFromSendPacket)
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost PacketCountOverflow / Decrement RefCount = %d)! \n", session->sessionId, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
		return;
	}

	//----------------------------------------------------------------
	// 현 락프리큐 구현은 ABA로 인한 일시적 큐 끊김이 발생할 가능성이 존재
	// - 락프리큐 size와 실제 Dequeue가능한 size가 일치하지 않을 가능성이 있다.
	// - 이에 대해서 별도의 Dequeue가 성공한 개수(sendPacketCount)를 계산한다.
	//----------------------------------------------------------------
	int sendPacketCount = 0;
	for (int i = 0; i < sendQSize; i++)
	{
		CPacket* packet;
		bool ret = session->sendLFQ->Dequeue(packet);
		if (ret == false)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / LFQ Dequeue Fail! (ABA => size != dequeueCount)\n", session->sessionId);
			break;
		}
		wsaBufArr[i].buf = packet->GetBufferPtr();
		wsaBufArr[i].len = packet->GetDataSize();

		// Session에서 CPacketPool로의 반환을 위한 포인터 관리
		session->sendOlp.freePackets[i] = packet;
		++sendPacketCount;
	}

	if (sendPacketCount != sendQSize) // 테스트용 (이 현상이 자주 있는지 확인하기 위해) (나중에 지우기)
		__debugbreak();

	session->sendOlp.sendCount = sendPacketCount;
	//------------------------------------------------------------------------------------------
	// - 만약에 위 문제로 인해서 sendPacketCount가 0이라면 SendPost는 취소한다.
	// 
	// [ 예외 처리 ]
	// - 이 시점에 소유권으로 인해 다른 SendPacket에서 SendPost가 실패한다면 마지막 Enqueue된 패킷은 전송되지 못한다.
	// - 소유권 포기 이후, 한 번더 size를 체크하여 재시도를 수행한다.
	//------------------------------------------------------------------------------------------
	if (sendPacketCount == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"id : %016llx  / SendPacketCount = 0)! \n", session->sessionId);
		InterlockedExchange(&session->isSending, false);
		if (session->sendLFQ->size > 0)
			SendPost(session, false);

		if (bCallFromSendPacket)
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost PacketCountOverflow / Decrement RefCount = %d)! \n", session->sessionId, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
		return;
	}



	//--------------------------------------------------------
	// WSASend 호출
	//--------------------------------------------------------
	if (!bCallFromSendPacket)
	{
		ULONG refCount = InterlockedIncrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost / Increment RefCount = %d)! \n", session->sessionId, refCount);
	}

	memset(&session->sendOlp, 0, sizeof(WSAOVERLAPPED));
	DWORD sendBytes;
	int sendRet = WSASend(session->sock, wsaBufArr, sendPacketCount, &sendBytes, 0, (WSAOVERLAPPED*)&session->sendOlp, nullptr);
	if (sendRet == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"Send (FAST I/O) / sendBytes : %d \n", sendBytes);
	}
	else if (sendRet == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error == WSA_IO_PENDING)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L" Send IO PENDING\n");
		}
		else
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendError / errorCode = %d / Decrement RefCount = %d)! \n", session->sessionId, error, refCount);
			if (refCount == 0)
				ReleaseSession(session);
		}
	}
	PRO_END("SendPost");
	return;
}

void CLanClient::SendPacket(ULONGLONG sessionId, CPacket* packet)
{
	PRO_BEGIN("SendPacket_Unicast");
	InterlockedIncrement(&sendMessageTPS);
	InterlockedIncrement(&packet->refCount);
	Session* session = AcquireSessionById(sessionId);
	if (session == nullptr)
	{
		InterlockedDecrement(&packet->refCount);
		return;
	}
	session->sendLFQ->Enqueue(packet);
	SendPost(session, true);

	PRO_END("SendPacket_Unicast");
	return;
}

Session* CLanClient::AcquireSessionById(ULONGLONG sessionId)
{
	Session* session = this->session;
	//-----------------------------------------------
	// 세션 참조 카운트 증가 후 찾은 세션이 유효한지 검사
	// - Release에 진입한 세션인지 (RefCount의 최상위 비트를 ReleaseFlag로 활용)
	// - 해당 세션이 이미 삭제되어 같은 메모리에 다른 세션이 들어와있는 경우인지
	// 
	// [ 유의 사항 ]
	// RefCount 증가 및 Release 진입 여부 확인 이후에 세션이 바뀌었는지 검사가 필요
	// - 위 시점 이후부터 세션이 바뀌지 않음을 보장받을 수 있기 때문
	//-----------------------------------------------
	ULONG refCount = InterlockedIncrement(&session->refCount);
	_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / parameter id : %016llx / AcquireSessionById / Increment RefCount = %d)! \n", session->sessionId, sessionId, refCount);

	ULONG releaseFlag = session->refCount & (1 << 31);
	if (releaseFlag)
	{
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / parameter id : %016llx / AcquireSessionById - Already Release Enter Session\n", session->sessionId, sessionId);
		if (refCount == 0)
			ReleaseSession(session);
		return nullptr;
	}

	//-----------------------------------------------
	// 이 시점부터 session은 도중에 바뀌지 않음
	// - 세션 변경 검사의 유효성이 보장된다.
	//-----------------------------------------------
	if (session->sessionId != sessionId)
	{
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / parameter id : %016llx / AcquireSessionById - Target Session Changed / Decrement RefCount = %d \n", session->sessionId, sessionId, refCount);
		if (refCount == 0)
			ReleaseSession(session);
		return nullptr;
	}

	return session;
}

unsigned int CLanClient::IOCPWorkerProc(void* arg)
{
	CLanClient* core = static_cast<CLanClient*>(arg);

	while (1)
	{
		//--------------------- GQCS 예외 처리---------------------------
		// 1. GQCS가 false을 반환
		//		(1) CP 핸들이 닫힌 경우 (또는 타임 아웃) -> Dequeue 실패 -> overlapped == null
		//		(2) 연결이 파괴된 경우(RST) overlapped != null, transferred = 0
		// 2. GQCS가 true를 반환
		//		- I/O 성공 및 Dequeue 성공
		//--------------------------------------------------------------
		DWORD transferred = 0;
		SessionOlp* sessionOlp;
		Session* session;
		bool gqcsRet = GetQueuedCompletionStatus(core->hCp, &transferred, (PULONG_PTR)&session, (LPOVERLAPPED*)&sessionOlp, INFINITE);

		PRO_BEGIN("CompletionRoutine");
		//--------------------------------------------------------------
		// lpOverlapped가 null인지 확인 필요 
		//  - CP 핸들이 닫힌 경우 (또는 dwMillisecond 타임 아웃) -> Dequeue 실패
		//  - 이 때 completion Key와 transferred는 과거 값 그대로 남아있기 때문에, 엉뚱한 세션에 잘못된 로직이 돌 가능성이 있으므로 무조건 체크
		//	
		// Dequeue 실패에 대한 예외 처리를 따로 분류해서 하지 않음을 선택
		//	- PQCS 종료 신호에 따른 워커 스레드 종료 처리에 대한 예외
		//	- overlapped null, transferred 0, completion key 0
		//	- 이에 대한 예외가 상단에 들어갔다면 결국 overlapped null이면 워커 스레드 종료를 타게 됨
		//	- PQCS 종료 신호에 대한 예외처리로 overlapped가 null임을 일괄 처리 (나머지 멤버도 0으로 초기화를 하니까)
		//--------------------------------------------------------------
		if (sessionOlp == nullptr)
		{
			_LOG(dfLOG_LEVEL_SYSTEM, L"Completion Status - Overlapped is null!\n");
			return 0;
		}


		// ------------------------------------------
		// [ transferred가 0이 되는 상황 ]
		// - RST로 인한 I/O 실패
		// - FIN으로 인한 I/O 성공
		// 
		// [ transferred가 0일 때 어떻게 처리할 것인가? ]
		// - 즉시 세션 연결 끊는 것은 불가하며, RefCount 감소 후 연결 끊기를 시도한다.
		// ------------------------------------------
		if (transferred == 0)
		{
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - Transferred = 0 / Decrement RefCount = %d\n", session->sessionId, refCount);
			if (refCount == 0)
				core->ReleaseSession(session);
			continue;
		}


		//---------------------------------------------------------
		// Recv 완료 처리
		//---------------------------------------------------------
		if (sessionOlp->type == ERecv)
		{
			RecvOlp* sendOlp = (RecvOlp*)&sessionOlp;
			session->recvQ->MoveWritePos(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016llx / CompletionPort : Recv  / transferred : %d------------\n", session->sessionId, transferred);
			while (1)
			{
				//-------------------------------------------
				// 완성된 메시지 추출
				//-------------------------------------------
				st_PacketHeader header;
				int peekRet = session->recvQ->PeekData((char*)&header, sizeof(header));
				if (peekRet < sizeof(header))
					break;
				if (session->recvQ->GetDataSize() < sizeof(header) + header.payloadLen)
					break;
				session->recvQ->MoveReadPos(sizeof(st_PacketHeader));
				CPacketViewer* message = CPacketViewer::AllocPacketViewer(session->recvQ, header.payloadLen);
				core->OnMessage(0, message);
				CPacketViewer::ReleasePacketViewer(message);
				session->recvQ->MoveReadPos(header.payloadLen);
				InterlockedIncrement(&core->recvMessageTPS);
			}

			// ------------------------------------------
			// 다시 Recv 걸기
			// ------------------------------------------
			ULONG refCount = InterlockedIncrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - BeforeRecvPost / InCrement RefCount = %d\n", session->sessionId, refCount);
			core->RecvPost(session);
		}
		else if (sessionOlp->type == ESend)
		{
			SendOlp* sendOlp = (SendOlp*)&sessionOlp;
			//---------------------------------------------------------
			// Send 완료 처리
			//  - 사용한 직렬화 버퍼 반납
			//  - 송신 중임을 나타내는 플래그 비활성화
			//  - Send 송신 중에 SendQ에 전송할 데이터가 쌓였다면 다시 Send 
			//---------------------------------------------------------
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016llx / CompletionPort : Send  / transferred : %d------------\n", session->sessionId, transferred);
			for (int i = 0; i < session->sendOlp.sendCount; i++)
				CPacket::ReleaseSendPacket(session->sendOlp.freePackets[i]);
			session->sendOlp.sendCount = 0;

			InterlockedExchange(&session->isSending, false);

			if (session->sendLFQ->size > 0)
			{
				core->SendPost(session, false);
			}
		}


		// ----------------------------------------------------------------
		// IOCount 감소 후, 세션 정리 시점 확인
		// ----------------------------------------------------------------
		ULONG refCount = InterlockedDecrement(&session->refCount);

		if (sessionOlp->type == ESend)
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - AfterRoutine [Send] / Decrement RefCount = %d\n", session->sessionId, refCount);
		else
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - AfterRoutine [Recv] / Decrement RefCount = %d\n", session->sessionId, refCount);


		if (refCount == 0)
			core->ReleaseSession(session);

		PRO_END("CompletionRoutine");
	}
	return 0;
}

unsigned int CLanClient::MonitoringThreadProc(void* arg)
{
	CLanClient* core = static_cast<CLanClient*>(arg);

	while (1)
	{
		DWORD ret = WaitForSingleObject(core->shutdownEvent, 1000);
		if (ret == WAIT_OBJECT_0)
		{
			_LOG(dfLOG_LEVEL_SYSTEM, L"shutdown event is signal - MonitoringThread Exit!");
			break;
		}
		core->OnMonitoring();
	}

	return 0;
}

