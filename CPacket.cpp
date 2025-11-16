#include "CPacket.h"

//-----------------------------------------------------------------------------------
// 오브젝트 풀 선언부
//-----------------------------------------------------------------------------------
//CObjectPool_LF<CPacket, true> CPacket::recvPacketPool(true, 0, 1, 4096);
//CObjectPool_LF<CPacket, true> CPacket::sendPacketPool(true, 0, 2, 1024);
CObjectPool_TLS<CPacket, true> CPacket::recvPacketPool(true, 0, 1, 4096);
CObjectPool_TLS<CPacket, true> CPacket::sendPacketPool(true, 0, 2, 1024);
CObjectPool_TLS<CPacketViewer, true> CPacketViewer::packetViewerPool(true, 0, 3);


//-----------------------------------------------------------------------------------
// CPacket 구현부
//-----------------------------------------------------------------------------------
CPacket& CPacket::operator=(CPacket& clSrcPacket)
{
	int writePos = clSrcPacket.useSize;
	__int64 readPos = clSrcPacket.readPtr - clSrcPacket.buffer;
	memcpy_s(buffer, bufferMaxSize, clSrcPacket.buffer, clSrcPacket.useSize);
	bufferMaxSize = clSrcPacket.bufferMaxSize;
	useSize = clSrcPacket.useSize;
	readPtr = buffer + readPos;
	writePtr = buffer + writePos;
	return *this;
}

int CPacket::GetData(char* chpDest, int iSize)
{
	if (iSize > useSize)
		return 0;
	memcpy_s(chpDest, iSize, readPtr, iSize);
	readPtr += iSize;
	useSize -= iSize;
	return iSize;
}

int CPacket::PeekData(char* chpDest, int iSize)
{
	if (iSize > useSize)
		return 0;
	memcpy_s(chpDest, iSize, readPtr, iSize);

	return iSize;
}

int CPacket::PutData(char* chpSrc, int iSrcSize)
{
	if (writePtr + iSrcSize > buffer + bufferMaxSize)
		return 0;

	memcpy_s(writePtr, buffer + bufferMaxSize - writePtr, chpSrc, iSrcSize);
	writePtr += iSrcSize;
	useSize += iSrcSize;
	return iSrcSize;
}


CPacket* CPacket::AllocSendPacket()
{
	CPacket* packet = sendPacketPool.allocObject();
	packet->Clear();
	packet->refCount++;
	return packet;
};

CPacket* CPacket::AllocRecvPacket()
{
	CPacket* packet = recvPacketPool.allocObject();
	packet->Clear();
	packet->refCount++;
	return packet;
};

void CPacket::ReleaseSendPacket(CPacket* packet)
{
	ULONG refCount = InterlockedDecrement(&packet->refCount);
	if (refCount == 0)
		sendPacketPool.freeObject(packet);
	return;
};

void CPacket::ReleaseRecvPacket(CPacket* packet)
{
	ULONG refCount = InterlockedDecrement(&packet->refCount);
	if (refCount == 0)
		recvPacketPool.freeObject(packet);
	return;
};





//-----------------------------------------------------------------------------------
// CPacketViewer 구현부
//-----------------------------------------------------------------------------------
int CPacketViewer::GetData(char* chpDest, int iSize)
{
	if (iSize > useSize)
		return 0;
	memcpy_s(chpDest, iSize, readPtr, iSize);
	readPtr += iSize;
	useSize -= iSize;
	return iSize;
}
int CPacketViewer::PeekData(char* chpDest, int iSize)
{
	if (iSize > useSize)
		return 0;
	memcpy_s(chpDest, iSize, readPtr, iSize);
	return iSize;
}
CPacketViewer* CPacketViewer::AllocPacketViewer(CPacket* refPacket, ULONG dataSize)
{
	CPacketViewer* packetViewer = packetViewerPool.allocObject();
	packetViewer->SetRefPacket(refPacket, dataSize);
	packetViewer->refCount++;
	InterlockedIncrement(&refPacket->refCount);
	return packetViewer;
}
void CPacketViewer::ReleasePacketViewer(CPacketViewer* packetViewer)
{
	ULONG viewerRefCount = InterlockedDecrement(&packetViewer->refCount);
	if (viewerRefCount == 0)
	{
		CPacket::ReleaseRecvPacket(packetViewer->packet);
		CPacketViewer::packetViewerPool.freeObject(packetViewer);
	}
	return;
}

