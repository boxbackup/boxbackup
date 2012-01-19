#pragma once

#ifdef WIN32
#	ifdef _MSC_VER

#	elif defined __MINGW32__

#		ifndef __MSVCRT_VERSION__
#			define __MSVCRT_VERSION__ 0x0601
#		endif

#	endif

#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <WinSock2.h>
#	include <Shlobj.h>
#	include <Shlwapi.h>
#	include <WinCrypt.h>

#	include <fcntl.h>
#	include <sys/stat.h>
#	include <direct.h>
#	include <errno.h>
#	include <io.h>
#	include <stdlib.h>
#	include <string.h>
#	include <stdio.h>
#	include <stdarg.h>
#	include <time.h>

#	include <cstdint>
#	include <memory>
#	include <string>

#	include "Win32Exception.h"


	namespace BoxBackup
	{
		namespace Win32
		{
			extern void wide2multi(const std::wstring& wide, std::string& multi, const UINT CodePage = CP_UTF8);
			extern std::string wide2multi(const std::wstring& wide, const UINT CodePage = CP_UTF8);
			extern void multi2wide(const std::string& multi, std::wstring& wide, const UINT CodePage = CP_UTF8);
			extern std::wstring multi2wide(const std::string& multi, const UINT CodePage = CP_UTF8);

			extern std::string GetCurrentDirectory();
			extern std::string GetErrorMessage(DWORD errorCode);

			extern std::string ConvertPathToAbsoluteUnicode(const char *pFileName) throw(Win32Exception);
			extern HANDLE OpenFileByNameUtf8(const char* pFileName, DWORD flags) throw(Win32Exception);

			extern bool EnableBackupRights();
		}
	}

#endif
