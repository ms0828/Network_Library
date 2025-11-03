#include "Log.h"
#include <iostream>

static int g_LogLevel;
static ELogMode g_LogMode;
static FILE* g_LogFile;
thread_local wchar_t gt_LogBuf[LOG_BUFFER_LEN];


bool InitLog(int logLevel, ELogMode logMode)
{
	g_LogLevel = logLevel;
	g_LogMode = logMode;

	if (ELogMode::NOLOG)
		return true;


	//---------------------------------------------------------
	// 로그 파일 제목 설정 및 파일 오픈
	//---------------------------------------------------------
	if (logMode == ELogMode::FILE_DIRECT)
	{
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

const wchar_t* LogLevelToString(int logLevel)
{
	switch (logLevel)
	{
	case dfLOG_LEVEL_DEBUG:
		return L"DEBUG";
	case dfLOG_LEVEL_ERROR:
		return L"ERROR";
	case dfLOG_LEVEL_SYSTEM:
		return L"SYSTEM";
	}
	return L"UNKNOWN";
}


void Log(int level, const wchar_t* fmt, ...)
{
	if (g_LogMode == ELogMode::NOLOG)
		return;

	if (level < g_LogLevel)
		return;


	DWORD tid = GetCurrentThreadId();
	const wchar_t* levelStr = LogLevelToString(level);
	int prefixLen = swprintf_s(gt_LogBuf, LOG_BUFFER_LEN, L"[TID:%u]_[%ls] : ", tid, levelStr);
	if (prefixLen < 0)
		prefixLen = 0;

	va_list ap;
	va_start(ap, fmt);
	HRESULT hr = StringCchVPrintfW(gt_LogBuf + prefixLen, LOG_BUFFER_LEN - prefixLen, fmt, ap);
	va_end(ap);

	size_t len = wcslen(gt_LogBuf);
	if (len < LOG_BUFFER_LEN - 1)
	{
		gt_LogBuf[len] = L'\n';
		gt_LogBuf[len + 1] = L'\0';
	}


	//-----------------------------
	// 쓰기
	//-----------------------------
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

bool CloseLog()
{
	if (g_LogFile != nullptr)
	{
		fflush(g_LogFile);
		fclose(g_LogFile);
		return true;
	}

	return true;
}

