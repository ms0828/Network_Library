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
	bool			bStartFlag;			// 프로파일의 사용 여부. (배열시에만)
	bool			bInitFlag;		// 리셋이후, 처음 초기화인지 확인
	const char*		szName;			// 프로파일 샘플 이름.

	LARGE_INTEGER	lStartTick;			// 프로파일 샘플 실행 시간.

	__int64			iTotalTick;			// 전체 사용시간 카운터 Tick.	(출력시 호출회수로 나누어 평균 구함)
	__int64			iMin[2];			// 최소 사용시간 카운터 Tick.	(초단위로 계산하여 출력 / [0] 가장최소 [1] 다음 최소 [2])
	__int64			iMax[2];			// 최대 사용시간 카운터 Tick.	(초단위로 계산하여 출력 / [0] 가장최대 [1] 다음 최대 [2])

	__int64			iCall;				// 누적 호출 횟수.

}PROFILE_SAMPLE;


struct ThreadProfileEntry
{
	PROFILE_SAMPLE* arr;
	DWORD           tid;
	int* pEntryCount;
};

//-------------------------------------------------------------------------
// 스레드 시작 시 이 객체가 생성되고, 종료 시 파괴되며 자동 등록/해제를 수행
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
// Parameters: (char *) Profile Sample의 szName
// Return : 해당 엔트리의 배열 인덱스를 반환
//			해당 엔트리가 없다면 -1 반환
/////////////////////////////////////////////////////////////////////////////
int findProfileEntry(const char* szName);

/////////////////////////////////////////////////////////////////////////////
// 하나의 함수 Profiling 시작, 끝 함수.
//
// Parameters: (char *)Profiling이름.
// Return: 없음.
/////////////////////////////////////////////////////////////////////////////
void ProfileBegin(const char* szName);

void ProfileEnd(const char* szName);


/////////////////////////////////////////////////////////////////////////////
// Profiling 된 데이타를 Text 파일로 출력한다.
//
// Parameters: (char *)출력될 파일 이름.
// Return: 없음.
/////////////////////////////////////////////////////////////////////////////
void ProfileDataOutText(const char* szFileName);


/////////////////////////////////////////////////////////////////////////////
// 프로파일링 된 데이터를 모두 초기화 한다.
//
// Parameters: 없음.
// Return: 없음.
/////////////////////////////////////////////////////////////////////////////
void ProfileReset(void);


