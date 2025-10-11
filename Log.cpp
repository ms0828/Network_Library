#include "Log.h"
#include <process.h>
#include <iostream>
#include <TlHelp32.h>

static int g_LogLevel;
ELogMode g_LogMode;
static FILE* g_LogFile;
thread_local wchar_t gt_LogBuf[LOG_BUFFER_LEN];


bool InitLog(int logLevel, ELogMode logMode)
{
	g_LogLevel = logLevel;
	g_LogMode = logMode;

	if (ELogMode::NOLOG)
		return true;

	if (logMode == ELogMode::FILE_DIRECT)
	{
		//---------------------------------------------------------
		// 로그 파일 제목 설정
		//---------------------------------------------------------
		SYSTEMTIME systemTime;
		GetLocalTime(&systemTime);
		char logTitle[70];
		sprintf_s(logTitle, sizeof(logTitle), "Log_%04u-%02u-%02u_%02u-%02u-%02u.txt", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);

		errno_t ret;
		ret = fopen_s(&g_LogFile, (const char*)logTitle, "wt");
		if (ret != 0)
			return false;
	}
	

	return true;
}


void Log(int level, const wchar_t* fmt, ...)
{
	if (g_LogMode == ELogMode::NOLOG)
		return;

	if (level < g_LogLevel)
		return;

	DWORD tid = GetCurrentThreadId();
	int prefixLen = swprintf_s(gt_LogBuf, LOG_BUFFER_LEN, L"[TID:%u] ", tid);
	if (prefixLen < 0)
		prefixLen = 0;

	va_list ap;
	va_start(ap, fmt);
	HRESULT hr = StringCchVPrintfW(gt_LogBuf + prefixLen, LOG_BUFFER_LEN - prefixLen, fmt, ap);
	va_end(ap);

	if (g_LogMode == ELogMode::CONSOLE)
	{
		wprintf(gt_LogBuf);
	}
	else if(g_LogMode == ELogMode::FILE_DIRECT)
	{
		fputws(gt_LogBuf, g_LogFile);
		fflush(g_LogFile);
	}
	
}

