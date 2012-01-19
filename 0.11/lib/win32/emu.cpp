#include "Box.h"

#include <cstdio>


using namespace Win32;


struct passwd gTempPasswd;


// --------------------------------------------------------------------------
//
// Function
//		Name:    GetDefaultConfigFilePath(std::string name)
//		Purpose: Calculates the default configuration file name,
//			 by using the directory location of the currently
//			 executing program, and appending the provided name.
//			 In case of fire, returns an empty string.
//		Created: 26th May 2007
//
// --------------------------------------------------------------------------
std::string GetDefaultConfigFilePath(const std::string& rName) throw()
{
	// we just ask the system where to get the file...
	wchar_t path[MAX_PATH];

	try
	{
		if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_COMMON_APPDATA,NULL,0,path))
			&& PathAppendW(path,L"Box Backup")
			&& PathAppendW(path,multi2wide(rName.c_str()).c_str()))
		{
			return wide2multi(path);
		}
	}
	EMU_EXCEPTION_HANDLING_RETURN("")
}


int console_read(char* pBuffer, const size_t BufferSize) throw()
{
	HANDLE hConsole;
	
	try
	{
		if (INVALID_HANDLE_VALUE == (hConsole = GetStdHandle(STD_INPUT_HANDLE)))
			throw Win32Exception(Win32Exception::API_GetStdHandle);

		size_t wideSize = BufferSize / 5;
		std::unique_ptr<wchar_t[]> wide(new wchar_t[wideSize+1]);

		DWORD numCharsRead = 0;
		if (!ReadConsoleW(hConsole,wide.get(),static_cast<DWORD>(wideSize),&numCharsRead,NULL))
			throw Win32Exception(Win32Exception::API_ReadConsole);

		wide[numCharsRead] = '\0';
		std::string multi(wide2multi(wide.get()));
		strcpy(pBuffer,multi.c_str());

		return static_cast<int>(multi.size());
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}
