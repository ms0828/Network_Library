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
	// Session �迭 �޸� �Ҵ�
	//----------------------------------------------------------
	sessionArrSize = maxClientNum;
	sessionArr = new Session*[sessionArrSize]();
	for (int i = sessionArrSize - 1; i >= 0; i--)
		indexStack.push(i);

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

		//--------------------------------------------------------------
		// lpOverlapped�� null������ ������ Ȯ�� �ʿ� 
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

		USHORT arrIndex = core->GetSessionArrIndex(sessionId);
		Session* session = core->sessionArr[arrIndex];
		if (session->sessionId != sessionId)
		{
			_LOG(dfLOG_LEVEL_DEBUG, L"SendPacket - ���� id�� ��ġ���� �ʽ��ϴ�. �迭�� ���� id : %016 / ã������ ���� id : %016  \n", session->sessionId, sessionId);
			return false;
		}


		// ------------------------------------------
		// [ transferred�� 0�� �Ǵ� ��Ȳ ]
		// - RST�� ���� I/O ����
		// - FIN���� ���� I/O ����
		// 
		// [ transferred�� 0�� �� ó�� ]
		// - ��� ���� ���� �Ұ�
		// - ���� ���� �÷��׸� Ȱ��ȭ
		// ------------------------------------------
		if (transferred == 0)
		{
			_LOG(dfLOG_LEVEL_ERROR, L"Session id : %016 => �Ϸ� ���� transferred�� 0�Դϴ�.\n", session->sessionId);
			InterlockedExchange(&session->bDisconnect, true);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
				core->ReleaseSession(session);
			continue;
		}

		//---------------------------------------------------------
		// Recv �Ϸ� ó��
		//---------------------------------------------------------
		if (sessionOlp->type == ERecv)
		{
			session->recvQ->MoveRear(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016 / CompletionPort : Recv  / transferred : %d------------\n", session->sessionId, transferred);
	
			while (1)
			{
				//-------------------------------------------
				// �ϼ��� �޽��� ����
				//-------------------------------------------
				st_Header header;
				int peekLen = session->recvQ->Peek((char*)&header, sizeof(header));
				if (peekLen < sizeof(header))
					break;
				if (session->recvQ->GetUseSize() < sizeof(header) + header.payloadLen)
					break;
				session->recvQ->MoveFront(sizeof(header));


				//-------------------------------------------
				// �޽��� ����
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
			// �ٽ� Recv �ɱ�
			// ------------------------------------------
			core->RecvPost(session);
		}
		else if (sessionOlp->type == ESend)
		{
			//---------------------------------------------------------
			// Send �Ϸ� ó��
			//---------------------------------------------------------
			session->sendQ->MoveFront(transferred);
			_LOG(dfLOG_LEVEL_DEBUG, L"------------Session Id : %016 / CompletionPort : Send  / transferred : %d------------\n", session->sessionId, transferred);
			InterlockedExchange(&session->isSending, false);

			//---------------------------------------------------------
			// Send �۽� �߿� SendQ�� ������ �����Ͱ� �׿��ٸ� �ٽ� Send 
			//---------------------------------------------------------
			core->SendPost(session);
		}


		// ----------------------------------------------------------------
		// IOCount ���� ��, ���� ���� ���� Ȯ��
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
		AcquireSRWLockExclusive(&core->indexStackLock);
		ULONGLONG arrIndex = core->indexStack.top();
		core->indexStack.pop();
		ReleaseSRWLockExclusive(&core->indexStackLock);
		
		//------------------------------------------------------
		// ���� Id ���� ( ���� �迭 �ε���(2byte) + sessionIdCnt(6byte) )
		//------------------------------------------------------
		ULONGLONG sessionId = 0;
		sessionId += (arrIndex << 48);
		sessionId += core->sessionIdCnt;
		++core->sessionIdCnt;
		

		//------------------------------------------------------
		// ���� ���� �� SessionArr ���
		//------------------------------------------------------
		Session* newSession = new Session(clnSock, sessionId);
		CreateIoCompletionPort((HANDLE)newSession->sock, core->hCp, (ULONG_PTR)newSession->sessionId, 0);
		core->sessionArr[arrIndex] = newSession;
		printf("Login : %016llx\n", newSession->sessionId);


		//------------------------------------------------------
		// �ش� ������ OnAccept ȣ�� �� Recv ���
		//------------------------------------------------------
		core->OnAccept(&clnAdr, newSession->sessionId);
		core->RecvPost(newSession);
	}
	return 0;
}

void CLanServer::RecvPost(Session* session)
{
	//------------------------------------------------------------------
	// RecvPost�� ��� �Ǵ� ��Ȳ
	// 1. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 2. �ش� ������ RecvQ�� �� �� ���
	//		- �� ���� �ش� ������ Disconnect �÷��� Ȱ��ȭ�Ͽ� ���� ���� ����
	//-------------------------------------------------------------------
	if (session->bDisconnect)
		return;
	printf("------------AsyncRecv  session id : %016llx------------\n", session->sessionId);

	//------------------------------------------------------------------
	// RecvQ�� ���� WSABUF ����
	//-------------------------------------------------------------------
	WSABUF wsaRecvBufArr[2];
	int wsaBufCnt = 1;
	int totalFreeSize = session->recvQ->GetFreeSize();
	if (totalFreeSize == 0)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"RECV�� �� á���ϴ�\n");
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
	// �ش� ������ IOCount ���� �� WSARecv ȣ��
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
	// SendPost�� ��� �Ǵ� ��Ȳ
	// 1. ���� Send�� ���� ��
	// 2. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 3. �ش� ������ SendQ�� ����ִ� ���
	//-------------------------------------------------------
	if (InterlockedCompareExchange(&session->isSending, true, false) == true || session->bDisconnect)
		return;

	printf("------------AsyncSend  session id : %016llx------------\n", session->sessionId);
	
	//--------------------------------------------------------
	// SendQ�� ���� WSABUF ����
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
	// �ش� ������ IOCount ���� �� WSASend ȣ��
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
	// ���� �迭���� �ش� ���� �˻�
	// 
	// [���� ó��]
	// - ���� ã������ ���� ID�� ���� �迭���� ã�� session�� ID�� �ٸ��ٸ� false return
	//-------------------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	Session* session = sessionArr[arrIndex];
	if (session == nullptr)
		return false;
	if (session->sessionId != sessionId)
	{
		printf("SendPacket - ���� id�� ��ġ���� �ʽ��ϴ�. �迭�� ���� id : %016llx / ã������ ���� id : %016llx  \n", session->sessionId, sessionId);
		return false;
	}


	//-------------------------------------------------------
	// �ش� ������ SendQ Enqueue �� SendPost ȣ��
	// 
	// [���� ó��]
	// - �۽� ���� ������ ���ڶ�� ���� �÷��׸� Ȱ��ȭ
	// - ���� ���� �� ���� Release ����
	//-------------------------------------------------------
	int enqueueRet = session->sendQ->Enqueue(message->GetBufferPtr(), message->GetDataSize());
	if (enqueueRet == 0)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"sendQ ������ ���ڶ��ϴ�.\n");
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
	// 1. ���� �迭���� �ش� ���� ����
	// 2. ���� �迭 �ε��� ��ȯ
	//------------------------------------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(session->sessionId);
	sessionArr[arrIndex] = nullptr;
	AcquireSRWLockExclusive(&indexStackLock);
	indexStack.push(arrIndex);
	ReleaseSRWLockExclusive(&indexStackLock);
	
	
	// -------------------------------------------
	// ���� delete
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