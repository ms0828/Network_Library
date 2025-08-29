#include "NetLibrary.h"


CLanServer::CLanServer()
{
	hCp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	InitializeSRWLock(&indexStackLock);
	sessionIdCnt = 1;
	sessionArrSize = 0;
}

CLanServer::~CLanServer()
{
	delete[] iocpWorkerHandleArr;

	for (int i = 0; i < sessionArrSize; i++)
	{
		if (sessionArr[i] != nullptr)
			delete sessionArr[i];
	}
	delete[] sessionArr;
}

bool CLanServer::Start(PCWSTR servIp, USHORT servPort, ULONG numOfWorkerThread, ULONG maxClientNum)
{
	g_LogLevel = dfLOG_LEVEL_DEBUG;
	//----------------------------------------------------------
	// Session 배열 메모리 할당
	//----------------------------------------------------------
	sessionArrSize = maxClientNum;
	sessionArr = new Session*[sessionArrSize]();
	for (int i = sessionArrSize - 1; i >= 0; i--)
		indexStack.push(i);

	//----------------------------------------------------------
	// Completion Port 생성 및 워커 스레드 생성
	//----------------------------------------------------------
	
	iocpWorkerHandleArr = new HANDLE[numOfWorkerThread];
	for (int i = 0; i < numOfWorkerThread; i++)
		iocpWorkerHandleArr[i] = (HANDLE)_beginthreadex(nullptr, 0, IOCPWorkerProc, this, 0, nullptr);


	//----------------------------------------------------------
	// 소켓 라이브러리 초기화
	//----------------------------------------------------------
	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"WSAStartUp Fail");
		return false;
	}
	

	//----------------------------------------------------------
	// 리슨 소켓 생성
	//----------------------------------------------------------
	listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"listenSock Create Fail");
		return false;
	}

	//----------------------------------------------------------
	// closesocket 시 RST 송신
	//----------------------------------------------------------
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	int ret = setsockopt(listenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));


	//----------------------------------------------------------
	// I/O Pending 유도하기 위해 TCP 송신 버퍼 크기를 0으로 세팅
	//----------------------------------------------------------
	int optval = 0;
	ret = setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval));


	//----------------------------------------------------------
	// listenSocket 주소 바인딩
	//----------------------------------------------------------
	SOCKADDR_IN servAdr;
	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	InetPton(AF_INET, servIp, &servAdr.sin_addr);
	servAdr.sin_port = htons(servPort);
	int bindRet = bind(listenSock, (SOCKADDR*)&servAdr, sizeof(servAdr));
	if (bindRet == SOCKET_ERROR)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"Bind Fail");
		return false;
	}

	//----------------------------------------------------------
	// listen
	//----------------------------------------------------------
	int listenRet = listen(listenSock, SOMAXCONN);
	if (listenRet == SOCKET_ERROR)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"listen Fail");
		return false;
	}

	//----------------------------------------------------------
	// AccpetThread 생성 및 가동
	//----------------------------------------------------------
	acceptThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, AcceptProc, this, 0, nullptr);

	_LOG(dfLOG_LEVEL_SYSTEM, L"Server Start");

	return true;
}

void CLanServer::Stop()
{

}

bool CLanServer::DisconnectSession(ULONGLONG sessionId)
{
	return false;
}


unsigned int CLanServer::IOCPWorkerProc(void* arg)
{
	CLanServer* core = static_cast<CLanServer*>(arg);

	while (1)
	{
		//--------------------- GQCS 예외 처리---------------------------
		// 1. GQCS가 false을 반환
		//		(1) CP 핸들이 닫힌 경우 (또는 타임 아웃) -> Dequeue 실패 -> overlapped == null
		//		(2) 연결이 파괴된 경우(RST) overlapped != null, transferred = 0
		//			-> 그러나 I/O가 실패했다고 즉시 처리할 게 없다.
		//				- 멀티 스레드 환경에서는 I/O 실패를 인지 후에 I/O 성공 완료 통지가 처리될 수 있음
		//				- 그 자리에서 세션을 삭제한다거나.. 등의 처리가 불가
		// 2. GQCS가 true를 반환
		//		- I/O 성공 및 Dequeue 성공
		//--------------------------------------------------------------
		DWORD transferred = 0;
		ULONGLONG sessionId;
		SessionOverlapped* sessionOlp;
		bool gqcsRet = GetQueuedCompletionStatus(core->hCp, &transferred, (PULONG_PTR)&sessionId, (LPOVERLAPPED*)&sessionOlp, INFINITE);

		//--------------------------------------------------------------
		// lpOverlapped가 null인지는 무조건 확인 필요 
		// - CP 핸들이 닫힌 경우 (또는 dwMillisecond 타임 아웃) -> Dequeue 실패
		// - 이 때 completion Key와 transferred는 과거 값 그대로 남아있기 때문에, 엉뚱한 세션에 잘못된 로직이 돌 가능성이 있으므로 무조건 체크
		//	
		// 일반적으로 Dequeue 실패에 대한 예외 처리를 따로 분류해서 하지 않음	
		//		- PQCS 종료 신호에 따른 워커 스레드 종료 처리에 대한 예외
		//		- overlapped null, transferred 0, completion key 0
		//		- 이에 대한 예외가 상단에 들어갔다면 결국 overlapped null이면 워커 스레드 종료를 타게 됨
		//		- PQCS 종료 신호에 대한 예외처리로 overlapped가 null임을 일괄 처리하는게 일반적임 (나머지 멤버도 0으로 초기화를 하니까)
		//--------------------------------------------------------------
		if (sessionOlp == nullptr)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"overlapped null!!!!!!!!!!!!!!!!!!!!!!\n");
			return 0;
		}

		USHORT arrIndex = core->GetSessionArrIndex(sessionId);
		Session* session = core->sessionArr[arrIndex];
		if (session->sessionId != sessionId)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L"SendPacket - 세션 id가 일치하지 않습니다. 배열의 세션 id : %016 / 찾으려는 세션 id : %016  \n", session->sessionId, sessionId);
			return false;
		}


		// ------------------------------------------
		// [ transferred가 0이 되는 상황 ]
		// - RST로 인한 I/O 실패
		// - FIN으로 인한 I/O 성공
		// 
		// [ transferred가 0일 때 처리 ]
		// - 즉시 세션 삭제 불가
		// - 세션 종료 플래그를 활성화
		// ------------------------------------------
		if (transferred == 0)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"Session id : %016 => 완료 통지 transferred가 0입니다.\n", session->sessionId);
			InterlockedExchange(&session->bDisconnect, true);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
				core->ReleaseSession(session);
			continue;
		}

		//---------------------------------------------------------
		// Recv 완료 처리
		//---------------------------------------------------------
		if (sessionOlp->type == ERecv)
		{
			session->recvQ->MoveRear(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016 / CompletionPort : Recv  / transferred : %d------------\n", session->sessionId, transferred);
	
			while (1)
			{
				//-------------------------------------------
				// 완성된 메시지 추출
				//-------------------------------------------
				st_Header header;
				int peekLen = session->recvQ->Peek((char*)&header, sizeof(header));
				if (peekLen < sizeof(header))
					break;
				if (session->recvQ->GetUseSize() < sizeof(header) + header.payloadLen)
					break;
				session->recvQ->MoveFront(sizeof(header));


				//-------------------------------------------
				// 메시지 추출
				//-------------------------------------------
				char messageBuf[1024];
				int dequeueRet = session->recvQ->Dequeue(messageBuf, header.payloadLen);
				CPacket message;
				message.PutData((char*)&header, sizeof(header));
				int putDataRet = message.PutData(messageBuf, header.payloadLen);
				if (putDataRet == 0)
				{
					InterlockedExchange(&session->bDisconnect, true);
					break;
				}

				core->OnMessage(session->sessionId, &message);
			}

			// ------------------------------------------
			// 다시 Recv 걸기
			// ------------------------------------------
			core->RecvPost(session);
		}
		else if (sessionOlp->type == ESend)
		{
			//---------------------------------------------------------
			// Send 완료 처리
			//---------------------------------------------------------
			session->sendQ->MoveFront(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016 / CompletionPort : Send  / transferred : %d------------\n", session->sessionId, transferred);
			InterlockedExchange(&session->isSending, false);

			//---------------------------------------------------------
			// Send 송신 중에 SendQ에 전송할 데이터가 쌓였다면 다시 Send 
			//---------------------------------------------------------
			core->SendPost(session);
		}


		// ----------------------------------------------------------------
		// IOCount 감소 후, 세션 정리 시점 확인
		// ----------------------------------------------------------------
		if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
		{
			core->ReleaseSession(session);
			continue;
		}
	}
	return 0;
}

unsigned int CLanServer::AcceptProc(void* arg)
{
	CLanServer* core = static_cast<CLanServer*>(arg);

	while (1)
	{
		SOCKADDR_IN clnAdr;
		int clnAdrSz = sizeof(clnAdr);
		SOCKET clnSock = accept(core->listenSock, (SOCKADDR*)&clnAdr, &clnAdrSz);
		if (clnSock == INVALID_SOCKET)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"accpet Fail - ListenSocket이 닫혔는지 확인하세요.");
			break;
		}

		//----------------------------------------------------------
		// 연결을 허용할지 확인
		//----------------------------------------------------------
		bool isPossibleConnection = core->OnConnectionRequest(&clnAdr);
		if (isPossibleConnection == false)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L"OnConnectionRequest - 거부된 연결 요청입니다.");
			continue;
		}

		
		//------------------------------------------------------
		// 사용 가능한 세션 배열 인덱스 추출
		//------------------------------------------------------
		AcquireSRWLockExclusive(&core->indexStackLock);
		ULONGLONG arrIndex = core->indexStack.top();
		core->indexStack.pop();
		ReleaseSRWLockExclusive(&core->indexStackLock);
		
		//------------------------------------------------------
		// 세션 Id 생성 ( 세션 배열 인덱스(2byte) + sessionIdCnt(6byte) )
		//------------------------------------------------------
		ULONGLONG sessionId = 0;
		sessionId += (arrIndex << 48);
		sessionId += core->sessionIdCnt;
		++core->sessionIdCnt;
		

		//------------------------------------------------------
		// 세션 생성 및 SessionArr 등록
		//------------------------------------------------------
		Session* newSession = new Session(clnSock, sessionId);
		CreateIoCompletionPort((HANDLE)newSession->sock, core->hCp, (ULONG_PTR)newSession->sessionId, 0);
		core->sessionArr[arrIndex] = newSession;
		printf("Login : %016llx\n", newSession->sessionId);


		//------------------------------------------------------
		// 해당 세션의 OnAccept 호출 및 Recv 등록
		//------------------------------------------------------
		core->OnAccept(&clnAdr, newSession->sessionId);
		core->RecvPost(newSession);
	}
	return 0;
}

void CLanServer::RecvPost(Session* session)
{
	//------------------------------------------------------------------
	// RecvPost가 취소 되는 상황
	// 1. 해당 세션의 Disconnect 플래그가 활성화된 경우
	// 2. 해당 세션의 RecvQ가 꽉 찬 경우
	//		- 이 경우는 해당 세션의 Disconnect 플래그 활성화하여 세션 종료 유도
	//-------------------------------------------------------------------
	if (session->bDisconnect)
		return;
	printf("------------AsyncRecv  session id : %016llx------------\n", session->sessionId);

	//------------------------------------------------------------------
	// RecvQ에 대한 WSABUF 세팅
	//-------------------------------------------------------------------
	WSABUF wsaRecvBufArr[2];
	int wsaBufCnt = 1;
	int totalFreeSize = session->recvQ->GetFreeSize();
	if (totalFreeSize == 0)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"RECV가 꽉 찼습니다\n");
		InterlockedExchange(&session->bDisconnect, true);
		return;
	}
	int directEnqueueSize = session->recvQ->DirectEnqueueSize();
	wsaRecvBufArr[0].buf = session->recvQ->GetRearBufferPtr();
	wsaRecvBufArr[0].len = directEnqueueSize;
	if (directEnqueueSize < totalFreeSize)
	{
		int remainFreeSize = totalFreeSize - directEnqueueSize;
		wsaRecvBufArr[1].buf = session->recvQ->GetBufferPtr();
		wsaRecvBufArr[1].len = remainFreeSize;
		wsaBufCnt = 2;
	}

	//--------------------------------------------------------
	// 해당 세션의 IOCount 증감 후 WSARecv 호출
	//--------------------------------------------------------
	InterlockedIncrement((LONG*)&session->ioCount);
	DWORD flags = 0;
	int recvRet = WSARecv(session->sock, wsaRecvBufArr, wsaBufCnt, nullptr, &flags, (WSAOVERLAPPED*)&session->recvOlp, nullptr);
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
			_LOG(dfLOG_LEVEL_DEBUG, L"recv error : %d\n", error);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
			{
				ReleaseSession(session);
				return;
			}
		}
	}
	return;
}

void CLanServer::SendPost(Session* session)
{
	//--------------------------------------------------------
	// SendPost가 취소 되는 상황
	// 1. 현재 Send가 진행 중
	// 2. 해당 세션의 Disconnect 플래그가 활성화된 경우
	// 3. 해당 세션의 SendQ가 비어있는 경우
	//-------------------------------------------------------
	if (InterlockedCompareExchange(&session->isSending, true, false) == true || session->bDisconnect)
		return;

	printf("------------AsyncSend  session id : %016llx------------\n", session->sessionId);
	
	//--------------------------------------------------------
	// SendQ에 대한 WSABUF 세팅
	//--------------------------------------------------------
	WSABUF wsaSendBufArr[2];
	int wsaBufCnt = 1;
	int totalUseSize = session->sendQ->GetUseSize();
	if (totalUseSize == 0)
	{
		InterlockedExchange(&session->isSending, false);
		return;
	}
	int directDequeueSize = session->sendQ->DirectDequeueSize();
	wsaSendBufArr[0].buf = session->sendQ->GetFrontBufferPtr();
	wsaSendBufArr[0].len = directDequeueSize;
	if (directDequeueSize < totalUseSize)
	{
		int remainUseSize = totalUseSize - directDequeueSize;
		wsaSendBufArr[1].buf = session->sendQ->GetBufferPtr();
		wsaSendBufArr[1].len = remainUseSize;
		wsaBufCnt = 2;
	}

	//--------------------------------------------------------
	// 해당 세션의 IOCount 증감 후 WSASend 호출
	//--------------------------------------------------------
	InterlockedIncrement((LONG*)&session->ioCount);
	DWORD sendBytes;
	int sendRet = WSASend(session->sock, wsaSendBufArr, wsaBufCnt, &sendBytes, 0, (WSAOVERLAPPED*)&session->sendOlp, nullptr);
	if (sendRet == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"Send (FAST I/O) / sendBytes : %d \n",sendBytes);
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
			_LOG(dfLOG_LEVEL_DEBUG, L"send error : %d\n", error);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
			{
				ReleaseSession(session);
				return;
			}
		}
	}
	return;
}


bool CLanServer::SendPacket(ULONGLONG sessionId, CPacket* message)
{
	//-------------------------------------------------------
	// 세션 배열에서 해당 세션 검색
	// 
	// [예외 처리]
	// - 현재 찾으려는 세션 ID와 세션 배열에서 찾은 session의 ID가 다르다면 false return
	//-------------------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	Session* session = sessionArr[arrIndex];
	if (session == nullptr)
		return false;
	if (session->sessionId != sessionId)
	{
		printf("SendPacket - 세션 id가 일치하지 않습니다. 배열의 세션 id : %016llx / 찾으려는 세션 id : %016llx  \n", session->sessionId, sessionId);
		return false;
	}


	//-------------------------------------------------------
	// 해당 세션의 SendQ Enqueue 및 SendPost 호출
	// 
	// [예외 처리]
	// - 송신 버퍼 공간이 모자라면 종료 플래그를 활성화
	// - 연결 종료 및 세션 Release 유도
	//-------------------------------------------------------
	int enqueueRet = session->sendQ->Enqueue(message->GetBufferPtr(), message->GetDataSize());
	if (enqueueRet == 0)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"sendQ 공간이 모자랍니다.\n");
		InterlockedExchange(&session->bDisconnect, true);
		return false;
	}

	SendPost(session);

	return true;
}


void CLanServer::ReleaseSession(Session* session)
{
	printf("TryReleaseSession - id : %016llx", session->sessionId);

	//------------------------------------------------------------------------
	// 1. 세션 배열에서 해당 세션 제거
	// 2. 세션 배열 인덱스 반환
	//------------------------------------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(session->sessionId);
	sessionArr[arrIndex] = nullptr;
	AcquireSRWLockExclusive(&indexStackLock);
	indexStack.push(arrIndex);
	ReleaseSRWLockExclusive(&indexStackLock);
	
	
	// -------------------------------------------
	// 세션 delete
	// -------------------------------------------
	closesocket(session->sock);
	delete session;
	return;
}

USHORT CLanServer::GetSessionArrIndex(ULONGLONG sessionId)
{
	USHORT sessionArrIndex = (sessionId >> 48);

	//printf("id : %016llx -> GetSessionArrIndex : %d\n", sessionId, sessionArrIndex);
	return sessionArrIndex;
}