#include "ServerLog.h"
#include <iostream>


int g_LogLevel = dfLOG_LEVEL_SYSTEM;	// ��� ���� ��� �α� ����
wchar_t g_LogBuf[1024]; // �α� ����� �ʿ��� �ӽ� ����

void Log(wchar_t* logStr, int logLevel)
{
	g_LogLevel = dfLOG_LEVEL_DEBUG;
	if (logLevel >= g_LogLevel)
	{
		wprintf(L"%s\n", logStr);
	};
}