#include "NetLibrary.h"
#include "CPacket.h"
#include "ObjectPool.h"
#include "Log.h"


CLanServer::CLanServer()
{
	hCp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	listenSock = INVALID_SOCKET;
	sessionArr = nullptr;
	sessionIdCnt = 1;
	numOfMaxSession = 0;
	iocpWorkerHandleArr = nullptr;
	acceptThreadHandle = nullptr;

	InitLog(dfLOG_LEVEL_ERROR, ELogMode::FILE_DIRECT);
}

CLanServer::~CLanServer()
{
	delete[] iocpWorkerHandleArr;
	delete[] sessionArr;
}

bool CLanServer::Start(PCWSTR servIp, USHORT servPort, ULONG numOfWorkerThread, ULONG maxSessionNum)
{
	//----------------------------------------------------------
	// Session 배열 메모리 할당
	//----------------------------------------------------------
	numOfMaxSession = maxSessionNum;
	sessionArr = new Session[numOfMaxSession]();
	for(int i = numOfMaxSession - 1; i >= 0; i--)
	{
		indexStack.Push((USHORT &)i);
	}


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


		PRO_BEGIN("CompletionRoutine");
		//--------------------------------------------------------------
		// lpOverlapped가 null인지 확인 필요 
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


		// ------------------------------------------
		// 세션 검색
		// ------------------------------------------
		Session* session = core->FindSessionById(sessionId);
		if (session == nullptr)
			continue;


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
			session->bRecvRST = true;
			//InterlockedExchange(&session->bDisconnect, true);
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
				session->recvQ->MoveReadPos(sizeof(header));
				core->OnMessage(session->sessionId, session->recvQ);
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
			//---------------------------------------------------------
			// Send 완료 처리
			//  - 사용한 직렬화 버퍼 반납
			//  - 송신 중임을 나타내는 플래그 비활성화
			//  - Send 송신 중에 SendQ에 전송할 데이터가 쌓였다면 다시 Send 
			//---------------------------------------------------------
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016llx / CompletionPort : Send  / transferred : %d------------\n", session->sessionId, transferred);
			for (int i = 0; i < session->sendPacketCount; i++)
			{
				CPacket::sendCPacketPool.freeObject(session->freePacket[i]);
			}
			InterlockedExchange(&session->isSending, false);

			if(session->sendLFQ->size > 0)
			{
				core->SendPost(session, false);
			}
		}


		// ----------------------------------------------------------------
		// IOCount 감소 후, 세션 정리 시점 확인
		// ----------------------------------------------------------------
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - AfterRoutine / Decrement RefCount = %d\n", session->sessionId, refCount);
		if (refCount == 0)
		{
			//InterlockedExchange(&session->bDisconnect, true);
			core->ReleaseSession(session);
		}
		PRO_END("CompletionRoutine");
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
		ULONGLONG arrIndex = 0;
		bool ret = core->indexStack.Pop((USHORT&)arrIndex);
		if (ret == false)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"error : indexStack Pop Fail  -- stackSize = %d\n", core->indexStack.stackSize);
			closesocket(clnSock);
			continue;
		}

		

		//------------------------------------------------------
		// 세션 Id 생성 ( 세션 배열 인덱스(2byte) + sessionIdCnt(6byte) )
		//------------------------------------------------------
		ULONGLONG sessionId = 0;
		sessionId += (arrIndex << 48);
		sessionId += core->sessionIdCnt;
		++core->sessionIdCnt;
		

		//------------------------------------------------------
		// 세션 배열에 새로운 세션 할당 및 초기화
		//------------------------------------------------------
		Session& newSession = core->sessionArr[arrIndex];
		newSession.InitSession(clnSock, sessionId);
		CreateIoCompletionPort((HANDLE)newSession.sock, core->hCp, (ULONG_PTR)newSession.sessionId, 0);
		printf("Login : %016llx\n", newSession.sessionId);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Login And Init / arrIndex = %d\n", sessionId, arrIndex);


		//------------------------------------------------------
		// 해당 세션의 OnAccept 호출 및 Recv 등록
		//------------------------------------------------------
		core->OnAccept(&clnAdr, newSession.sessionId);
		core->RecvPost(&newSession);
	}
	return 0;
}

void CLanServer::RecvPost(Session* session)
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
	CPacket* newRecvQ = CPacket::recvCPacketPool.allocObject();
	newRecvQ->Clear();
	if (session->recvQ != nullptr)
	{
		if (session->recvQ->GetDataSize() > 0)
			int putRet = newRecvQ->PutData(session->recvQ->GetReadPtr(), session->recvQ->GetDataSize());
		CPacket::recvCPacketPool.freeObject(session->recvQ);
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
		//InterlockedExchange(&session->bDisconnect, true);
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"session id : %016llx / recvQ is Full / Decrement RefCount = %d \n", session->sessionId, refCount);
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
			//InterlockedExchange(&session->bDisconnect, true);
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / RecvError / errorCode = %d / Decrement RefCount = %d)! \n", session->sessionId, error, refCount);
			if (refCount == 0)
				ReleaseSession(session);

			session->bRecvRST = true;
		}
	}

	PRO_END("RecvPost");
	return;
}

void CLanServer::SendPost(Session* session, bool bCallFromSendPacket)
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
	WSABUF wsaBufArr[MAXSENDPACKETCOUNT];
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
		return;
	}
	if (sendQSize > MAXSENDPACKETCOUNT)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"packetCount overflow / id : %016llx------------\n", session->sessionId);
		//InterlockedExchange(&session->bDisconnect, true);
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost PacketCountOverflow / Decrement RefCount = %d)! \n", session->sessionId, refCount);
		if (refCount == 0)
			ReleaseSession(session);
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
		session->freePacket[i] = packet;
		++sendPacketCount;
	}
	session->sendPacketCount = sendPacketCount;
	//------------------------------------------------------------------------------------------
	// - 만약에 위 문제로 인해서 sendPacketCount가 0이라면 SendPost는 취소한다.
	// 
	// [ 예외 처리 ]
	// - 이 시점에 소유권으로 인해 다른 SendPacket에서 SendPost가 실패한다면 마지막 Enqueue된 패킷은 전송되지 못한다.
	// - 소유권 포기 이후, 한 번더 size를 체크하여 재시도를 수행한다.
	//------------------------------------------------------------------------------------------
	if (session->sendPacketCount == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"id : %016llx  / SendPacketCount = 0)! \n", session->sessionId);
		InterlockedExchange(&session->isSending, false);
		if (session->sendLFQ->size > 0)
			SendPost(session, false);
		return;
	}
	
	

	//--------------------------------------------------------
	// WSASend 호출
	//--------------------------------------------------------
	if(!bCallFromSendPacket)
	{
		ULONG refCount = InterlockedIncrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPost / Increment RefCount = %d)! \n", session->sessionId, refCount);
	}

	memset(&session->sendOlp, 0, sizeof(WSAOVERLAPPED));
	DWORD sendBytes;
	int sendRet = WSASend(session->sock, wsaBufArr, sendQSize, &sendBytes, 0, (WSAOVERLAPPED*)&session->sendOlp, nullptr);
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
			//InterlockedExchange(&session->bDisconnect, true);
			ULONG refCount = InterlockedDecrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendError / errorCode = %d / Decrement RefCount = %d)! \n", session->sessionId, error, refCount);
			if (refCount == 0)
				ReleaseSession(session);
			session->bRecvRST = true;
		}
	}
	PRO_END("SendPost");
	return;
}


bool CLanServer::SendPacket(ULONGLONG sessionId, CPacket* packet)
{
	PRO_BEGIN("SendPacket");

	//-----------------------------------------------
	// 세션 검색 및 세션 확보
	//-----------------------------------------------
	Session* session = AcquireSessionById(sessionId);
	if (session == nullptr)
		return false;


	//-------------------------------------------------------
	// 해당 세션의 SendQ Enqueue 및 SendPost 호출
	//-------------------------------------------------------
	session->sendLFQ->Enqueue(packet);
	SendPost(session, true);

	PRO_END("SendPacket");
	return true;
}


void CLanServer::ReleaseSession(Session* session)
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


	//------------------------------------------------------------------------
	// 해당 세션 메모리 초기화 및 배열 인덱스 반환
	//------------------------------------------------------------------------
	closesocket(session->sock);
	USHORT arrIndex = GetSessionArrIndex(session->sessionId);
	indexStack.Push(arrIndex);
	return;
}

USHORT CLanServer::GetSessionArrIndex(ULONGLONG sessionId)
{
	USHORT sessionArrIndex = (sessionId >> 48);
	return sessionArrIndex;
}

CLanServer::Session* CLanServer::AcquireSessionById(ULONGLONG sessionId)
{
	//----------------------------------------------
	// 해당 세션 Id로 부터 배열 인덱스 획득 및 세션 검색
	//----------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	if (arrIndex < 0 || arrIndex > numOfMaxSession - 1)
		return nullptr;
	Session* session = &sessionArr[arrIndex];
	if (session->sessionId == 0)
		return nullptr;

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
	_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / arrIndex = %d / parameter id : %016llx / AcquireSessionById / Increment RefCount = %d)! \n", session->sessionId, arrIndex, sessionId, refCount);

	ULONG releaseFlag = session->refCount & (1 << 31);
	if (releaseFlag)
	{
		ULONG refCount = InterlockedDecrement(&session->refCount);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / arrIndex = %d / parameter id : %016llx / AcquireSessionById - Already Release Enter Session\n", session->sessionId, arrIndex, sessionId);
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
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / arrIndex = %d / parameter id : %016llx / AcquireSessionById - Target Session Changed / Decrement RefCount = %d \n", session->sessionId, arrIndex, sessionId, refCount);
		if (refCount == 0)
			ReleaseSession(session);
		return nullptr;
	}
	
	return session;
}

CLanServer::Session* CLanServer::FindSessionById(ULONGLONG sessionId)
{
	//----------------------------------------------
	// 해당 세션 Id로 부터 배열 인덱스 획득 및 세션 검색
	//----------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	if (arrIndex < 0 || arrIndex > numOfMaxSession - 1)
		return nullptr;
	Session* session = &sessionArr[arrIndex];


	//-----------------------------------------------
	// 찾은 세션이 유효한지 검사
	// - 해당 세션이 이미 삭제되어 같은 메모리에 다른 세션이 들어와있는 경우인지
	//-----------------------------------------------
	if (session->sessionId != sessionId)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"FindSessionById - session id is not correct. array session id : %016llx / find session id : %016llx  \n", session->sessionId, sessionId);
		return nullptr;
	}

	return session;
}
