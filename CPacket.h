#pragma once

#define DEFAULT_BUFSIZE 1400

class CPacket
{
public:

	CPacket();
	CPacket(int iBufferSize);

	~CPacket();


	//////////////////////////////////////////////////////////////////////////
	// ���� �ʱ�ȭ
	// Parameters: ����.
	// Return: ����.
	//////////////////////////////////////////////////////////////////////////
	void Clear(void);


	//////////////////////////////////////////////////////////////////////////
	// ���� ������ ���
	//
	// Parameters: ����.
	// Return: (int)��Ŷ ���� ������ ���.
	//////////////////////////////////////////////////////////////////////////
	int	GetBufferSize(void) { return bufferMaxSize; }
	//////////////////////////////////////////////////////////////////////////
	// ���� ������� ������ ���.
	//
	// Parameters: ����.
	// Return: (int)������� ����Ÿ ������.
	//////////////////////////////////////////////////////////////////////////
	int	GetDataSize(void) { return useSize; }



	//////////////////////////////////////////////////////////////////////////
	// ���� ������ ���.
	//
	// Parameters: ����.
	// Return: (char *)���� ������.
	//////////////////////////////////////////////////////////////////////////
	char* GetBufferPtr(void) { return buffer; }


	//////////////////////////////////////////////////////////////////////////
	// ���� Pos �̵�. (�����̵��� �ȵ�)
	// GetBufferPtr �Լ��� �̿��Ͽ� �ܺο��� ������ ���� ������ ������ ��� ���. 
	//
	// Parameters: (int) �̵� ������.
	// Return: (int) �̵��� ������.
	//////////////////////////////////////////////////////////////////////////
	int	MoveWritePos(int iSize);
	int	MoveReadPos(int iSize);




	/* ============================================================================= */
	// ������ �����ε�
	/* ============================================================================= */
	CPacket& operator = (CPacket& clSrcPacket);

	//////////////////////////////////////////////////////////////////////////
	// �ֱ�.	�� ���� Ÿ�Ը��� ��� ����.
	//////////////////////////////////////////////////////////////////////////
	CPacket& operator << (unsigned char byValue);
	CPacket& operator << (char chValue);

	CPacket& operator << (unsigned short wValue);
	CPacket& operator << (short shValue);
	
	CPacket& operator << (unsigned int iValue);
	CPacket& operator << (int iValue);

	CPacket& operator << (unsigned long lValue);
	CPacket& operator << (long lValue);

	CPacket& operator << (float fValue);
	CPacket& operator << (double dValue);

	CPacket& operator << (unsigned __int64 iValue);
	CPacket& operator << (__int64 iValue);





	//////////////////////////////////////////////////////////////////////////
	// ����.	�� ���� Ÿ�Ը��� ��� ����.
	//////////////////////////////////////////////////////////////////////////
	CPacket& operator >> (unsigned char& byValue);
	CPacket& operator >> (char& chValue);

	CPacket& operator >> (unsigned short& wValue);
	CPacket& operator >> (short& shValue);
	
	CPacket& operator >> (unsigned int& iValue);
	CPacket& operator >> (int& iValue);

	CPacket& operator >> (unsigned long& dwValue);
	CPacket& operator >> (long& dwValue);

	CPacket& operator >> (float& fValue);
	CPacket& operator >> (double& dValue);

	CPacket& operator >> (unsigned __int64& iValue);
	CPacket& operator >> (__int64& iValue);
	



	//////////////////////////////////////////////////////////////////////////
	// ����Ÿ ���.
	//
	// Parameters: (char *)Dest ������. (int)Size.
	// Return: (int)������ ������.
	//////////////////////////////////////////////////////////////////////////
	int	GetData(char* chpDest, int iSize);

	//////////////////////////////////////////////////////////////////////////
	// ����Ÿ ����.
	//
	// Parameters: (char *)Src ������. (int)SrcSize.
	// Return: (int)������ ������.
	//////////////////////////////////////////////////////////////////////////
	int	PutData(char* chpSrc, int iSrcSize);




protected:
	char* buffer;
	char* readPtr;
	char* writePtr;
	
	int	bufferMaxSize;
	int	useSize;
};

