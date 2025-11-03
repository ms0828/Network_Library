#pragma once
#include <stdarg.h>
#include <strsafe.h> 
#include <Windows.h>


#define dfLOG_LEVEL_DEBUG 0
#define dfLOG_LEVEL_ERROR 1
#define dfLOG_LEVEL_SYSTEM 2

#define LOG_BUFFER_LEN 1024


//----------------------------------------------
// 로그 출력 모드
// 1. 콘솔
//  - _LOG(dfLOG_LEVEL_~, L"~") 매크로를 이용합니다.
// 2. 즉시 파일 쓰기
//  - _LOG(dfLOG_LEVEL_~, L"~") 매크로를 이용합니다.
//----------------------------------------------
#define _LOG(Level, ...)               \
do{                                    \
    Log((Level), __VA_ARGS__);         \
} while (0)                            \


enum ELogMode
{
    NOLOG = 0,
    CONSOLE,
    FILE_DIRECT,
};


//----------------------------------------------
// 로그 출력 모드에 따른 리소스 초기화
//----------------------------------------------
bool InitLog(int logLevel, ELogMode logMode);

//----------------------------------------------
// 로그 출력 모드에 따른 로그 출력
//----------------------------------------------
void Log(int level, const wchar_t* fmt, ...);


bool CloseLog();