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
	sessionArrSize = 0;
	iocpWorkerHandleArr = nullptr;
	acceptThreadHandle = nullptr;

	InitLog(dfLOG_LEVEL_ERROR, ELogMode::FILE_DIRECT);
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
	//----------------------------------------------------------
	// Session �迭 �޸� �Ҵ�
	//----------------------------------------------------------
	sessionArrSize = maxClientNum;
	sessionArr = new Session*[sessionArrSize]();
	for(int i = sessionArrSize - 1; i >= 0; i--)
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

		Session* session = core->FindSession(sessionId);
		if (session == nullptr)
			continue;

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
			_LOG(dfLOG_LEVEL_ERROR, L"Session id : %016llx => completion port transferred = 0.\n", session->sessionId);
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
	PRO_BEGIN("RecvPost");
	//------------------------------------------------------------------
	// RecvPost�� ��� �Ǵ� ��Ȳ
	// 1. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 2. �ش� ������ RecvQ�� �� �� ���
	//		- �� ���� �ش� ������ Disconnect �÷��� Ȱ��ȭ�Ͽ� ���� ���� ����
	//-------------------------------------------------------------------
	if (session->bDisconnect)
		return;
	
	_LOG(dfLOG_LEVEL_DEBUG, L"------------AsyncRecv  session id : %016llx------------\n", session->sessionId);

	//------------------------------------------------------------------
	// ���ο� recvQ(CPacket)�� CPacket Ǯ���� �Ҵ�
	//-------------------------------------------------------------------
	CPacket* newRecvQ = CPacket::recvCPacketPool.allocObject();
	newRecvQ->Clear();


	//---------------------------------------------------------------------------
	// ������ ����ߴ� recvQ(CPacket) �ݳ�
	// - ���� recvQ(CPacket)�� �̿ϼ��� �����Ͱ� �ִٸ� ���� �Ҵ��� recvQ(CPacket)�� �ű��
	//---------------------------------------------------------------------------
	if (session->recvQ != nullptr)
	{
		if (session->recvQ->GetDataSize() > 0)
		{
			int putRet = newRecvQ->PutData(session->recvQ->GetReadPtr(), session->recvQ->GetDataSize());	
			//------------------------------------------------
			// recvQ(CPacket)�� �� �� ���
			// - ������� ���� ū �����Ͱ� �������Ƿ� ���� ����
			//------------------------------------------------
			if (putRet == 0)
			{
				_LOG(dfLOG_LEVEL_ERROR, L"recvQ�� �� á���ϴ�.\n");
				InterlockedExchange(&session->bDisconnect, true);
				if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
				{
					ReleaseSession(session);
					return;
				}
			}
		}

		CPacket::recvCPacketPool.freeObject(session->recvQ);
	}

	//------------------------------------------------------------------
	// �ش� session�� �Ҵ� ���� recvQ(CPacket) ������ ���
	//-------------------------------------------------------------------
	session->recvQ = newRecvQ;

	//------------------------------------------------------------------
	// RecvQ�� ���� WSABUF ����
	//-------------------------------------------------------------------
	WSABUF wsaRecvBuf;
	wsaRecvBuf.buf = newRecvQ->GetBufferPtr();
	wsaRecvBuf.len = newRecvQ->GetBufferSize();
	


	//--------------------------------------------------------
	// �ش� ������ IOCount ���� �� WSARecv ȣ��
	//--------------------------------------------------------
	InterlockedIncrement((LONG*)&session->ioCount);
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
			_LOG(dfLOG_LEVEL_DEBUG, L"recv error : %d\n", error);
			InterlockedExchange(&session->bDisconnect, true);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
			{
				ReleaseSession(session);
				return;
			}
		}
	}

	PRO_END("RecvPost");
	return;
}

void CLanServer::SendPost(Session* session)
{
	PRO_BEGIN("SendPost");
	//--------------------------------------------------------
	// SendPost�� ��� �Ǵ� ��Ȳ
	// 1. ���� Send�� ���� ��
	// 2. �ش� ������ Disconnect �÷��װ� Ȱ��ȭ�� ���
	// 3. �ش� ������ SendQ�� ����ִ� ���
	//--------------------------------------------------------
	if (InterlockedCompareExchange(&session->isSending, true, false) == true || session->bDisconnect)
		return;

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
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / LFQ size = 0\n", session->sessionId);
		InterlockedExchange(&session->isSending, false);

		//------------------------------------------------------------------------------------------
		// [ ���� ó�� ]
		// size�� 0���� ������, �� ������ ���������� ���� �ٸ� SendPacket���� SendPost�� �����Ѵٸ� ������ Enqueue�� ��Ŷ�� ���۵��� ���Ѵ�.
		// => size�� 0�� ������ ������ ���� ����, �� ���� size�� üũ�Ͽ� ��õ��� �����Ѵ�.
		//------------------------------------------------------------------------------------------
		if (session->sendLFQ->size > 0)
			SendPost(session);
		return;
	}
	if (sendQSize > MAXSENDPACKETCOUNT)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"���� �� �ִ� ��Ŷ �� �ʰ� / id : %016llx------------\n", session->sessionId);
		InterlockedExchange(&session->bDisconnect, true);
		if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
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
		_LOG(dfLOG_LEVEL_ERROR, L"id : %016llx  / SendPacketCount = 0)! \n", session->sessionId);
		InterlockedExchange(&session->isSending, false);

		if (session->sendLFQ->size > 0)
			SendPost(session);
		return;
	}
	


	//--------------------------------------------------------
	// �ش� ������ IOCount ���� �� WSASend ȣ��
	//--------------------------------------------------------
	InterlockedIncrement((LONG*)&session->ioCount);
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
			_LOG(dfLOG_LEVEL_DEBUG, L"send error : %d\n", error);
			InterlockedExchange(&session->bDisconnect, true);
			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
			{
				ReleaseSession(session);
				return;
			}
		}
	}
	PRO_END("SendPost");
	return;
}


bool CLanServer::SendPacket(ULONGLONG sessionId, CPacket* packet)
{
	PRO_BEGIN("SendPacket");
	Session* session = FindSession(sessionId);
	if (session == nullptr)
		return false;

	//-------------------------------------------------------
	// �ش� ������ SendQ Enqueue �� SendPost ȣ��
	//-------------------------------------------------------
	session->sendLFQ->Enqueue(packet);
	SendPost(session);

	PRO_END("SendPacket");
	return true;
}


void CLanServer::ReleaseSession(Session* session)
{
	_LOG(dfLOG_LEVEL_DEBUG,L"TryReleaseSession - id : %016llx", session->sessionId);

	//------------------------------------------------------------------------
	// 1. ���� �迭���� �ش� ���� ����
	// 2. ���� �迭 �ε��� ��ȯ
	//------------------------------------------------------------------------
	USHORT arrIndex = GetSessionArrIndex(session->sessionId);
	sessionArr[arrIndex] = nullptr;
	indexStack.Push(arrIndex);
	
	
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

CLanServer::Session* CLanServer::FindSession(ULONGLONG sessionId)
{
	USHORT arrIndex = GetSessionArrIndex(sessionId);
	Session* session = sessionArr[arrIndex];
	if (session == nullptr)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"FindSession - already deleted session / session id : %016llx \n", sessionId);
		return nullptr;
	}
	else if (session->sessionId != sessionId)
	{
		_LOG(dfLOG_LEVEL_ERROR, L"FindSession - session id is not correct. array session id : %016llx / find session id : %016llx  \n", session->sessionId, sessionId);
		return nullptr;
	}

	return session;
}
