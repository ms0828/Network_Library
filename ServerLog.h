#pragma once
#include <cstdarg>
#define dfLOG_LEVEL_DEBUG 0
#define dfLOG_LEVEL_ERROR 1
#define dfLOG_LEVEL_SYSTEM 2

extern int g_LogLevel;	// 출력 저장 대상 로그 레벨
extern wchar_t g_LogBuf[1024]; // 로그 저장시 필요한 임시 버퍼

#define _LOG(LogLevel, fmt, ...)										\
do{																		\
	if(LogLevel >= g_LogLevel)											\
	{																	\
		wsprintf(g_LogBuf, fmt, ##__VA_ARGS__);							\
		Log(g_LogBuf, LogLevel);										\
	}																	\
}while(0)																\

void Log(wchar_t* logStr, int logLevel);

