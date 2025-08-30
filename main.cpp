#include <iostream>
#include <conio.h>
#include "CMyServer.h"

using namespace std;


int main()
{
	CMyServer server;
	server.Start(L"0.0.0.0", 6000, 5, 5000);
	
	
	while (1)
	{
		if (_kbhit())
		{
			WCHAR ControlKey = _getwch();

			if (ControlKey == L's' || ControlKey == L'S')
			{

			}
		}
	}

}