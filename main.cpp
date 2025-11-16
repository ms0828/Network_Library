#include <iostream>
#include <conio.h>
#include "CEchoServer.h"
#include <Windows.h>
#include "Log.h"
#include "CPacket.h"

using namespace std;

int main()
{
	InitLog(dfLOG_LEVEL_ERROR, ELogMode::NOLOG);
	CEchoServer server;
	server.Start(L"0.0.0.0", 6000, 5, 5000);
	

	DWORD sPressedTick = 0;
	BOOL waitingForS = FALSE;
	while (true)
	{
		// 1) r키를 누르면 프로파일링 리셋
		if (_kbhit())
		{
			int ch = _getch();

			if (ch == 's' || ch == 'S')
			{
				ProfileReset();
				printf("ProfileReset And Start Profile\n");
				waitingForS = TRUE;
				sPressedTick = GetTickCount64();
			}
			else if (ch == 27) // ESC 키로 종료
			{
				server.Stop();
				Sleep(3000);
				break;
			}
			else if (ch == 'd' || ch == 'D')
			{
				for (int i = 0; i < server.numOfMaxSession; i++)
				{
					bool bConnect = !(server.sessionArr[i].refCount & (1 << 31));
					if (bConnect)
						server.DisconnectSession(server.sessionArr[i].sessionId);
				}
			}
		}

		// 2) s키 누르고 10초 후 프로파일링 저장
		if (waitingForS)
		{
			DWORD now = GetTickCount64();
			if (now - sPressedTick >= 10000)
			{
				ProfileDataOutText("Profile_TLS6_500.txt");
				printf("ProfileDataOutText\n");
				waitingForS = FALSE;
			}
		}
	}

	CloseLog();

}