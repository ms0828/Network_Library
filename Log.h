#pragma once
#include <stdarg.h>
#include <strsafe.h> 


#define dfLOG_LEVEL_DEBUG 0
#define dfLOG_LEVEL_ERROR 1
#define dfLOG_LEVEL_SYSTEM 2

#define LOG_BUFFER_LEN 1024


#define _LOG(Level, ...)               \
do{                                    \
    Log((Level), __VA_ARGS__);         \
} while (0)                            \

bool InitLog(int logLevel);

void Log(int level, const wchar_t* fmt, ...);

