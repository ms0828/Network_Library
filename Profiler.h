#pragma once

#include <Windows.h>
#include <list>

#ifdef PROFILE
#define PRO_BEGIN(TagName)	ProfileBegin(TagName)
#define PRO_END(TagName)	ProfileEnd(TagName)
#else
#define PRO_BEGIN(TagName)
#define PRO_END(TagName)
#endif


#define SAMPLE_ARRAY_SIZE 100

typedef struct PROFILE_SAMPLE
{
	bool			bStartFlag;			// ���������� ��� ����. (�迭�ÿ���)
	bool			bInitFlag;		// ��������, ó�� �ʱ�ȭ���� Ȯ��
	const char*		szName;			// �������� ���� �̸�.

	LARGE_INTEGER	lStartTick;			// �������� ���� ���� �ð�.

	__int64			iTotalTick;			// ��ü ���ð� ī���� Tick.	(��½� ȣ��ȸ���� ������ ��� ����)
	__int64			iMin[2];			// �ּ� ���ð� ī���� Tick.	(�ʴ����� ����Ͽ� ��� / [0] �����ּ� [1] ���� �ּ� [2])
	__int64			iMax[2];			// �ִ� ���ð� ī���� Tick.	(�ʴ����� ����Ͽ� ��� / [0] �����ִ� [1] ���� �ִ� [2])

	__int64			iCall;				// ���� ȣ�� Ƚ��.

}PROFILE_SAMPLE;


struct ThreadProfileEntry
{
	PROFILE_SAMPLE* arr;
	DWORD           tid;
	int* pEntryCount;
};

//-------------------------------------------------------------------------
// ������ ���� �� �� ��ü�� �����ǰ�, ���� �� �ı��Ǹ� �ڵ� ���/������ ����
//-------------------------------------------------------------------------
class ThreadProfileRegistrar
{
public:
    ThreadProfileRegistrar();
	~ThreadProfileRegistrar();

private:
	std::list<ThreadProfileEntry>::iterator myIt;
};

/////////////////////////////////////////////////////////////////////////////
// Parameters: (char *) Profile Sample�� szName
// Return : �ش� ��Ʈ���� �迭 �ε����� ��ȯ
//			�ش� ��Ʈ���� ���ٸ� -1 ��ȯ
/////////////////////////////////////////////////////////////////////////////
int findProfileEntry(const char* szName);

/////////////////////////////////////////////////////////////////////////////
// �ϳ��� �Լ� Profiling ����, �� �Լ�.
//
// Parameters: (char *)Profiling�̸�.
// Return: ����.
/////////////////////////////////////////////////////////////////////////////
void ProfileBegin(const char* szName);

void ProfileEnd(const char* szName);


/////////////////////////////////////////////////////////////////////////////
// Profiling �� ����Ÿ�� Text ���Ϸ� ����Ѵ�.
//
// Parameters: (char *)��µ� ���� �̸�.
// Return: ����.
/////////////////////////////////////////////////////////////////////////////
void ProfileDataOutText(const char* szFileName);


/////////////////////////////////////////////////////////////////////////////
// �������ϸ� �� �����͸� ��� �ʱ�ȭ �Ѵ�.
//
// Parameters: ����.
// Return: ����.
/////////////////////////////////////////////////////////////////////////////
void ProfileReset(void);


