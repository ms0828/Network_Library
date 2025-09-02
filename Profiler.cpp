#include "Profiler.h"
#include <iostream>
#include <string>

thread_local PROFILE_SAMPLE* gt_ProfileSampleArr;
thread_local ThreadProfileRegistrar gt_profileRegistrar;
thread_local int gt_profileEntryCount;

static std::list<ThreadProfileEntry> g_profileList;
static SRWLOCK g_ProfileListLock = SRWLOCK_INIT;



int findProfileEntry(const char* szName)
{
	for (int i = 0; i < gt_profileEntryCount; i++)
	{
		if (strcmp(gt_ProfileSampleArr[i].szName, szName) == 0)
			return i;
	}
	return -1;
}

void ProfileBegin(const char* szName)
{
	int index = findProfileEntry(szName);
	if (index == -1)
	{
		gt_ProfileSampleArr[gt_profileEntryCount].szName = szName;
		gt_ProfileSampleArr[gt_profileEntryCount].iCall = 1;
		gt_ProfileSampleArr[gt_profileEntryCount].bStartFlag = true;
		QueryPerformanceCounter(&gt_ProfileSampleArr[gt_profileEntryCount].lStartTick);

		gt_profileEntryCount++;
		return;
	}
	else if (gt_ProfileSampleArr[index].bStartFlag)
	{
		QueryPerformanceCounter(&gt_ProfileSampleArr[index].lStartTick);
	}
	else
	{
		gt_ProfileSampleArr[index].iCall++;
		QueryPerformanceCounter(&gt_ProfileSampleArr[index].lStartTick);
		gt_ProfileSampleArr[index].bStartFlag = true;
	}
}

void ProfileEnd(const char* szName)
{
	int index = findProfileEntry(szName);
	if (index == -1)
	{
		printf("ProfileBegin���� �ʾҴ� ProfileEnd�� ȣ��\n");
		return;
	}

	LARGE_INTEGER EndTick;
	QueryPerformanceCounter(&EndTick);
	LONGLONG consumedTick = EndTick.QuadPart - gt_ProfileSampleArr[index].lStartTick.QuadPart;


	//----------------------------------------------------
	// ó�� ����ϴ� ������ ���
	//----------------------------------------------------
	if (gt_ProfileSampleArr[index].bInitFlag == false)
	{
		gt_ProfileSampleArr[index].iMax[0] = consumedTick;
		gt_ProfileSampleArr[index].iMax[1] = consumedTick;
		gt_ProfileSampleArr[index].iMin[0] = consumedTick;
		gt_ProfileSampleArr[index].iMin[1] = consumedTick;
		gt_ProfileSampleArr[index].bInitFlag = true;

		gt_ProfileSampleArr[index].iTotalTick += consumedTick;
		gt_ProfileSampleArr[index].bStartFlag = false;
		return;
	}


	//----------------------------------------------------
	// Max �� Min ������Ʈ
	//----------------------------------------------------
	if (gt_ProfileSampleArr[index].iMax[0] < consumedTick)
	{
		gt_ProfileSampleArr[index].iMax[1] = gt_ProfileSampleArr[index].iMax[0];
		gt_ProfileSampleArr[index].iMax[0] = consumedTick;
	}
	else if (gt_ProfileSampleArr[index].iMax[1] < consumedTick)
	{
		gt_ProfileSampleArr[index].iMax[1] = consumedTick;
	}

	if (gt_ProfileSampleArr[index].iMin[0] > consumedTick)
	{
		gt_ProfileSampleArr[index].iMin[1] = gt_ProfileSampleArr[index].iMin[0];
		gt_ProfileSampleArr[index].iMin[0] = consumedTick;
	}
	else if (gt_ProfileSampleArr[index].iMin[1] > consumedTick)
	{
		gt_ProfileSampleArr[index].iMin[1] = consumedTick;
	}

	// Total Tick ������Ʈ
	gt_ProfileSampleArr[index].iTotalTick += consumedTick;
	gt_ProfileSampleArr[index].bStartFlag = false;
}


void ProfileDataOutText(const char* szFileName)
{
	FILE* fp;
	errno_t ret;
	ret = fopen_s(&fp, szFileName, "wt");
	if (ret != 0)
	{
		printf("�������Ϸ� ���� ������ �����Ͽ����ϴ�\n");
		return;
	}

	AcquireSRWLockExclusive(&g_ProfileListLock);
	for (ThreadProfileEntry& threadProfileEntry : g_profileList)
	{
		int threadEntryCount = *threadProfileEntry.pEntryCount;
		if (threadEntryCount == 0)
			continue;

		// ������ �������ϸ� ��� ���
		char buffer[256];
		sprintf_s(buffer, sizeof(buffer), "-------------------------------------Thread id : %lu------------------------------------\n", threadProfileEntry.tid);
		fputs(buffer, fp);
		sprintf_s(buffer, sizeof(buffer), "        Name        |        Average        |           Min           |           Max           |           Call           |\n");
		fputs(buffer, fp);
		sprintf_s(buffer, sizeof(buffer), "------------------------------------------------------------------------------------\n");
		fputs(buffer, fp);
		
		// ������ �������ϸ� ���� ���
		for (int i = 0; i < threadEntryCount; i++)
		{
			//-------------------------------------------------------------
			// ������ 8���� Freq�� 1000,0000 ����
			//  T = 1 / 10, 000, 000 = 0.0000001��
			//	= 0.0001�и���
			//	= 0.1����ũ����
			//	= 100������
			//-------------------------------------------------------------
			LARGE_INTEGER Freq;
			QueryPerformanceFrequency(&Freq);
			double consumedTotalTime_us = threadProfileEntry.arr[i].iTotalTick / (double)Freq.QuadPart * 1'000'000;
			double consumedMaxTime_us = threadProfileEntry.arr[i].iMax[1] / (double)Freq.QuadPart * 1'000'000;
			double consumedMinTime_us = threadProfileEntry.arr[i].iMin[1] / (double)Freq.QuadPart * 1'000'000;
			double averageTime_us = 0.0;
			if (threadProfileEntry.arr[i].iCall > 2)
			{
				double max0_us = threadProfileEntry.arr[i].iMax[0] / (double)Freq.QuadPart * 1'000'000;
				double min0_us = threadProfileEntry.arr[i].iMin[0] / (double)Freq.QuadPart * 1'000'000;
				averageTime_us = (consumedTotalTime_us - max0_us - min0_us) / (threadProfileEntry.arr[i].iCall - 2);
			}
			else if (threadProfileEntry.arr[i].iCall != 0)
				averageTime_us = consumedTotalTime_us / threadProfileEntry.arr[i].iCall;

			sprintf_s(buffer, sizeof(buffer), "%20s | %17.4lf�� | %17.4lf�� | %17.4lf�� | %17lld |\n", threadProfileEntry.arr[i].szName, averageTime_us, consumedMinTime_us, consumedMaxTime_us, threadProfileEntry.arr[i].iCall);
			fputs(buffer, fp);
		}
	}
	ReleaseSRWLockExclusive(&g_ProfileListLock);
	
	fclose(fp);
}

void ProfileReset(void)
{
	AcquireSRWLockExclusive(&g_ProfileListLock);
	for (ThreadProfileEntry& threadProfileEntry : g_profileList)
	{
		int threadEntryCount = *threadProfileEntry.pEntryCount;
		if (threadEntryCount == 0)
			continue;
		for (int i = 0; i < threadEntryCount; i++)
		{
			threadProfileEntry.arr[i].bInitFlag = false;
			threadProfileEntry.arr[i].iCall = 0;
			threadProfileEntry.arr[i].iMax[0] = 0;
			threadProfileEntry.arr[i].iMax[1] = 0;
			threadProfileEntry.arr[i].iMin[0] = 0;
			threadProfileEntry.arr[i].iMin[1] = 0;
			threadProfileEntry.arr[i].iTotalTick = 0;
		}
	}
	ReleaseSRWLockExclusive(&g_ProfileListLock);
}

ThreadProfileRegistrar::ThreadProfileRegistrar()
{
	if (!gt_ProfileSampleArr)
	{
		gt_ProfileSampleArr = (PROFILE_SAMPLE*)malloc(sizeof(PROFILE_SAMPLE) * SAMPLE_ARRAY_SIZE);
		if (gt_ProfileSampleArr)
			memset(gt_ProfileSampleArr, 0, sizeof(PROFILE_SAMPLE) * SAMPLE_ARRAY_SIZE);
	}

	AcquireSRWLockExclusive(&g_ProfileListLock);
	myIt = g_profileList.insert(g_profileList.end(), ThreadProfileEntry{ gt_ProfileSampleArr, GetCurrentThreadId(), &gt_profileEntryCount });
	ReleaseSRWLockExclusive(&g_ProfileListLock);
}

ThreadProfileRegistrar::~ThreadProfileRegistrar()
{
	AcquireSRWLockExclusive(&g_ProfileListLock);
	g_profileList.erase(myIt);
	ReleaseSRWLockExclusive(&g_ProfileListLock);

	if (gt_ProfileSampleArr)
	{
		free(gt_ProfileSampleArr);
		gt_ProfileSampleArr = nullptr;
	}
}
