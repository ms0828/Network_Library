#include "ServerLog.h"
#include <iostream>


int g_LogLevel = dfLOG_LEVEL_SYSTEM;	// 출력 저장 대상 로그 레벨
wchar_t g_LogBuf[1024]; // 로그 저장시 필요한 임시 버퍼

void Log(wchar_t* logStr, int logLevel)
{
	g_LogLevel = dfLOG_LEVEL_DEBUG;
	if (logLevel >= g_LogLevel)
	{
		wprintf(L"%s\n", logStr);
	};
}