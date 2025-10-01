#include "Log.h"
#include <Windows.h>
#include <iostream>

static FILE* g_LogFile;
static int g_LogLevel;
static SRWLOCK g_LogLock;
thread_local wchar_t gt_LogBuf[LOG_BUFFER_LEN];

bool InitLog(int logLevel)
{
	InitializeSRWLock(&g_LogLock);

	g_LogLevel = logLevel;

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

	return true;
}


void Log(int level, const wchar_t* fmt, ...)
{
	if (level < g_LogLevel)
		return;

	DWORD tid = GetCurrentThreadId();
	// 먼저 Thread ID를 버퍼에 기록
	int prefixLen = swprintf_s(gt_LogBuf, LOG_BUFFER_LEN, L"[TID:%u] ", tid);
	if (prefixLen < 0)
		prefixLen = 0;

	va_list ap;
	va_start(ap, fmt);
	HRESULT hr = StringCchVPrintfW(gt_LogBuf + prefixLen, LOG_BUFFER_LEN - prefixLen, fmt, ap);
	va_end(ap);

	AcquireSRWLockExclusive(&g_LogLock);
	fputws(gt_LogBuf, g_LogFile);
	fflush(g_LogFile);
	ReleaseSRWLockExclusive(&g_LogLock);
}

