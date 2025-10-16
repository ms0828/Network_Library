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
	// Session �迭 �޸� �Ҵ�
	//----------------------------------------------------------
	numOfMaxSession = maxSessionNum;
	sessionArr = new Session[numOfMaxSession]();
	for(int i = numOfMaxSession - 1; i >= 0; i--)
	{
		indexStack.Push((USHORT &)i);
	}


	//----------------------------------------------------------
	// Completion Port ���� �� ��Ŀ ������ ����
	//----------------------------------------------------------
	iocpWorkerHandleArr = new HANDLE[numOfWorkerThread];
	for (int i = 0; i < numOfWorkerThread; i++)
		iocpWorkerHandleArr[i] = (HANDLE)_beginthreadex(nullptr, 0, IOCPWorkerProc, this, 0, nullptr);


	//----------------------------------------------------------
	// ���� ���̺귯�� �ʱ�ȭ
	//----------------------------------------------------------
	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"WSAStartUp Fail");
		return false;
	}
	

	//----------------------------------------------------------
	// ���� ���� ����
	//----------------------------------------------------------
	listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		_LOG(dfLOG_LEVEL_SYSTEM, L"listenSock Create Fail");
		return false;
	}

	//----------------------------------------------------------
	// closesocket �� RST �۽�
	//----------------------------------------------------------
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	int ret = setsockopt(listenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));


	//----------------------------------------------------------
	// I/O Pending �����ϱ� ���� TCP �۽� ���� ũ�⸦ 0���� ����
	//----------------------------------------------------------
	int optval = 0;
	ret = setsockopt(listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval));


	//----------------------------------------------------------
	// listenSocket �ּ� ���ε�
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
	// AccpetThread ���� �� ����
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
		//--------------------- GQCS ���� ó��---------------------------
		// 1. GQCS�� false�� ��ȯ
		//		(1) CP �ڵ��� ���� ��� (�Ǵ� Ÿ�� �ƿ�) -> Dequeue ���� -> overlapped == null
		//		(2) ������ �ı��� ���(RST) overlapped != null, transferred = 0
		//			-> �׷��� I/O�� �����ߴٰ� ��� ó���� �� ����.
		//				- ��Ƽ ������ ȯ�濡���� I/O ���и� ���� �Ŀ� I/O ���� �Ϸ� ������ ó���� �� ����
		//				- �� �ڸ����� ������ �����Ѵٰų�.. ���� ó���� �Ұ�
		// 2. GQCS�� true�� ��ȯ
		//		- I/O ���� �� Dequeue ����
		//--------------------------------------------------------------
		DWORD transferred = 0;
		ULONGLONG sessionId;
		SessionOverlapped* sessionOlp;
		bool gqcsRet = GetQueuedCompletionStatus(core->hCp, &transferred, (PULONG_PTR)&sessionId, (LPOVERLAPPED*)&sessionOlp, INFINITE);


		PRO_BEGIN("CompletionRoutine");
		//--------------------------------------------------------------
		// lpOverlapped�� null���� Ȯ�� �ʿ� 
		// - CP �ڵ��� ���� ��� (�Ǵ� dwMillisecond Ÿ�� �ƿ�) -> Dequeue ����
		// - �� �� completion Key�� transferred�� ���� �� �״�� �����ֱ� ������, ������ ���ǿ� �߸��� ������ �� ���ɼ��� �����Ƿ� ������ üũ
		//	
		// �Ϲ������� Dequeue ���п� ���� ���� ó���� ���� �з��ؼ� ���� ����	
		//		- PQCS ���� ��ȣ�� ���� ��Ŀ ������ ���� ó���� ���� ����
		//		- overlapped null, transferred 0, completion key 0
		//		- �̿� ���� ���ܰ� ��ܿ� ���ٸ� �ᱹ overlapped null�̸� ��Ŀ ������ ���Ḧ Ÿ�� ��
		//		- PQCS ���� ��ȣ�� ���� ����ó���� overlapped�� null���� �ϰ� ó���ϴ°� �Ϲ����� (������ ����� 0���� �ʱ�ȭ�� �ϴϱ�)
		//--------------------------------------------------------------
		if (sessionOlp == nullptr)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"overlapped null!!!!!!!!!!!!!!!!!!!!!!\n");
			return 0;
		}


		// ------------------------------------------
		// ���� �˻�
		// ------------------------------------------
		Session* session = core->FindSessionById(sessionId);
		if (session == nullptr)
			continue;


		// ------------------------------------------
		// [ transferred�� 0�� �Ǵ� ��Ȳ ]
		// - RST�� ���� I/O ����
		// - FIN���� ���� I/O ����
		// 
		// [ transferred�� 0�� �� ��� ó���� ���ΰ�? ]
		// - ��� ���� ���� ���� ���� �Ұ��ϸ�, RefCount ���� �� ���� ���⸦ �õ��Ѵ�.
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
		// Recv �Ϸ� ó��
		//---------------------------------------------------------
		if (sessionOlp->type == ERecv)
		{
			session->recvQ->MoveWritePos(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016llx / CompletionPort : Recv  / transferred : %d------------\n", session->sessionId, transferred);
			while (1)
			{
				//-------------------------------------------
				// �ϼ��� �޽��� ����
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
			// �ٽ� Recv �ɱ�
			// ------------------------------------------
			ULONG refCount = InterlockedIncrement(&session->refCount);
			_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Completion port - BeforeRecvPost / InCrement RefCount = %d\n", session->sessionId, refCount);
			core->RecvPost(session);
		}
		else if (sessionOlp->type == ESend)
		{
			//---------------------------------------------------------
			// Send �Ϸ� ó��
			//  - ����� ����ȭ ���� �ݳ�
			//  - �۽� ������ ��Ÿ���� �÷��� ��Ȱ��ȭ
			//  - Send �۽� �߿� SendQ�� ������ �����Ͱ� �׿��ٸ� �ٽ� Send 
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
		// IOCount ���� ��, ���� ���� ���� Ȯ��
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
			_LOG(dfLOG_LEVEL_ERROR, L"accpet Fail - ListenSocket�� �������� Ȯ���ϼ���.");
			break;
		}

		//----------------------------------------------------------
		// ������ ������� Ȯ��
		//----------------------------------------------------------
		bool isPossibleConnection = core->OnConnectionRequest(&clnAdr);
		if (isPossibleConnection == false)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L"OnConnectionRequest - �źε� ���� ��û�Դϴ�.");
			continue;
		}

		
		//------------------------------------------------------
		// ��� ������ ���� �迭 �ε��� ����
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
		// ���� Id ���� ( ���� �迭 �ε���(2byte) + sessionIdCnt(6byte) )
		//------------------------------------------------------
		ULONGLONG sessionId = 0;
		sessionId += (arrIndex << 48);
		sessionId += core->sessionIdCnt;
		++core->sessionIdCnt;
		

		//------------------------------------------------------
		// ���� �迭�� ���ο� ���� �Ҵ� �� �ʱ�ȭ
		//------------------------------------------------------
		Session& newSession = core->sessionArr[arrIndex];
		newSession.InitSession(clnSock, sessionId);
		CreateIoCompletionPort((HANDLE)newSession.sock, core->hCp, (ULONG_PTR)newSession.sessionId, 0);
		printf("Login : %016llx\n", newSession.sessionId);
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / Login And Init / arrIndex = %d\n", sessionId, arrIndex);


		//------------------------------------------------------
		// �ش� ������ OnAccept ȣ�� �� Recv ���
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
	// RecvPost�� ��� �Ǵ� ��Ȳ
	// 1. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 2. �ش� ������ RecvQ�� �� �� ���
	//		- �� ���� �ش� ������ Disconnect �÷��� Ȱ��ȭ�Ͽ� ���� ���� ����
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
	// 1. ���ο� recvQ(CPacket)�� CPacket Ǯ���� �Ҵ� �� ������ ����ߴ� recvQ(CPacket) �ݳ�
	//  - ���� recvQ(CPacket)�� �̿ϼ��� �����Ͱ� �ִٸ� ���� �Ҵ��� recvQ(CPacket)�� �ű��
	// 2. �ش� session�� �Ҵ� ���� recvQ(CPacket) ������ ���
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
	// WSARecv ȣ��
	//-------------------------------------------------------------------
	//_LOG(dfLOG_LEVEL_ERROR, L"Increment RefCount - RecvPost\n");
	memset(&session->recvOlp, 0, sizeof(WSAOVERLAPPED));
	WSABUF wsaRecvBuf;
	wsaRecvBuf.buf = newRecvQ->GetBufferPtr();
	wsaRecvBuf.len = newRecvQ->GetBufferSize() - newRecvQ->GetDataSize();
	if (wsaRecvBuf.len == 0)
	{
		// recvQ�� ������ ��Ȳ�� ����� �޽��� ũ�⸦ �ʰ��� �޽����� �� ��
		// - ���� ����� ��ó
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
	// SendPost�� ��� �Ǵ� ��Ȳ
	// 1. ���� Send�� ���� ��
	// 2. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 3. �ش� ������ SendQ�� ����ִ� ���
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
	// SendQ�� ���� WSABUF ����
	// - SendQ�� ��� ��Ŷ ���� 0�̶�� SendPost ���
	// - SendQ�� ��� ��Ŷ ���� �ִ� ���� �� �ִ� ��Ŷ �� ���� ���ٸ� ���������� ��Ȳ�̹Ƿ� �ش� ���� ����
	//--------------------------------------------------------
	WSABUF wsaBufArr[MAXSENDPACKETCOUNT];
	int sendQSize = session->sendLFQ->size;
	if (sendQSize == 0)
	{
		_LOG(dfLOG_LEVEL_DEBUG, L"id : %016llx  / LFQ size = 0\n", session->sessionId);
		InterlockedExchange(&session->isSending, false);

		//------------------------------------------------------------------------------------------
		// [ ���� ó�� ]
		// size�� 0���� ������, �� ������ ���������� ���� �ٸ� SendPacket���� SendPost�� �����Ѵٸ� ������ Enqueue�� ��Ŷ�� ���۵��� ���Ѵ�.
		// => size�� 0�� ������ ������ ���� ����, �� ���� size�� üũ�Ͽ� ��õ��� �����Ѵ�.
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
	// �� ������ť ������ ABA�� ���� �Ͻ��� ť ������ �߻��� ���ɼ��� ����
	// - ������ť size�� ���� Dequeue������ size�� ��ġ���� ���� ���ɼ��� �ִ�.
	// - �̿� ���ؼ� ������ Dequeue�� ������ ����(sendPacketCount)�� ����Ѵ�.
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

		// Session���� CPacketPool���� ��ȯ�� ���� ������ ����
		session->freePacket[i] = packet;
		++sendPacketCount;
	}
	session->sendPacketCount = sendPacketCount;
	//------------------------------------------------------------------------------------------
	// - ���࿡ �� ������ ���ؼ� sendPacketCount�� 0�̶�� SendPost�� ����Ѵ�.
	// 
	// [ ���� ó�� ]
	// - �� ������ ���������� ���� �ٸ� SendPacket���� SendPost�� �����Ѵٸ� ������ Enqueue�� ��Ŷ�� ���۵��� ���Ѵ�.
	// - ������ ���� ����, �� ���� size�� üũ�Ͽ� ��õ��� �����Ѵ�.
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
	// WSASend ȣ��
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
	// ���� �˻� �� ���� Ȯ��
	//-----------------------------------------------
	Session* session = AcquireSessionById(sessionId);
	if (session == nullptr)
		return false;


	//-------------------------------------------------------
	// �ش� ������ SendQ Enqueue �� SendPost ȣ��
	//-------------------------------------------------------
	session->sendLFQ->Enqueue(packet);
	SendPost(session, true);

	PRO_END("SendPacket");
	return true;
}


void CLanServer::ReleaseSession(Session* session)
{
	//------------------------------------------------------------------------
	// RefCount�� �ֻ��� ��Ʈ�� Release�� ��Ÿ���� Flag�� �ξ�, Release ���� �� ���� ������ ���´�.
	// - RefCount�� 0�̶�� ReleaseFlag Ȱ��ȭ �� Release ����
	// - RefCount�� 0�� �ƴ϶�� Release�� ���
	//------------------------------------------------------------------------
	ULONG releaseFlag = (1 << 31);
	if (InterlockedCompareExchange(&session->refCount, releaseFlag, 0) != 0)
		return;
	

	if (session->bRecvRST == false)
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / ReleaseSession With Not RST\n", session->sessionId);
	else
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx / ReleaseSession\n", session->sessionId);


	//------------------------------------------------------------------------
	// �ش� ���� �޸� �ʱ�ȭ �� �迭 �ε��� ��ȯ
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
	// �ش� ���� Id�� ���� �迭 �ε��� ȹ�� �� ���� �˻�
	//----------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	if (arrIndex < 0 || arrIndex > numOfMaxSession - 1)
		return nullptr;
	Session* session = &sessionArr[arrIndex];
	if (session->sessionId == 0)
		return nullptr;

	//-----------------------------------------------
	// ���� ���� ī��Ʈ ���� �� ã�� ������ ��ȿ���� �˻�
	// - Release�� ������ �������� (RefCount�� �ֻ��� ��Ʈ�� ReleaseFlag�� Ȱ��)
	// - �ش� ������ �̹� �����Ǿ� ���� �޸𸮿� �ٸ� ������ �����ִ� �������
	// 
	// [ ���� ���� ]
	// RefCount ���� �� Release ���� ���� Ȯ�� ���Ŀ� ������ �ٲ������ �˻簡 �ʿ�
	// - �� ���� ���ĺ��� ������ �ٲ��� ������ ������� �� �ֱ� ����
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
	// �� �������� session�� ���߿� �ٲ��� ����
	// - ���� ���� �˻��� ��ȿ���� ����ȴ�.
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
	// �ش� ���� Id�� ���� �迭 �ε��� ȹ�� �� ���� �˻�
	//----------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	if (arrIndex < 0 || arrIndex > numOfMaxSession - 1)
		return nullptr;
	Session* session = &sessionArr[arrIndex];


	//-----------------------------------------------
	// ã�� ������ ��ȿ���� �˻�
	// - �ش� ������ �̹� �����Ǿ� ���� �޸𸮿� �ٸ� ������ �����ִ� �������
	//-----------------------------------------------
	if (session->sessionId != sessionId)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"FindSessionById - session id is not correct. array session id : %016llx / find session id : %016llx  \n", session->sessionId, sessionId);
		return nullptr;
	}

	return session;
}
