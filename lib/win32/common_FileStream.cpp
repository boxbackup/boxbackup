#include "Box.h"


using namespace Win32;


static DWORD dwDesiredAccess(int flags) throw()
{
	DWORD desiredAccess = FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_EA;

	if		  (flags & O_WRONLY)	desiredAccess  = FILE_WRITE_DATA;
	else if (flags & O_RDWR)	desiredAccess |= FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_EA;

	return desiredAccess;
}
static DWORD dwShareMode(int flags) throw()
{
	DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

	if (flags & O_LOCK)	shareMode = 0;

	return shareMode;
}
static DWORD dwCreationDisposition(int flags) throw()
{
	DWORD creationDisposition = OPEN_EXISTING;

	if (flags & O_CREAT)
	{
		if		  (flags & O_EXCL)	creationDisposition = CREATE_NEW;
//		else if (flags & O_TRUNC)	creationDisposition = TRUNCATE_EXISTING;
		else								creationDisposition = OPEN_ALWAYS;
	}
	else if (flags & O_TRUNC)	creationDisposition = CREATE_ALWAYS;

	return creationDisposition;
}
static DWORD dwFlagsAndAttributes(int flags) throw()
{
	DWORD flagsAndAttributes = FILE_FLAG_BACKUP_SEMANTICS;

	if (flags & O_TEMPORARY)	flagsAndAttributes |= FILE_FLAG_DELETE_ON_CLOSE;

	return flagsAndAttributes;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    openfile
//		Purpose: replacement for any open calls - handles unicode
//			filenames - supplied in utf8
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
HANDLE openfile(const char *pFileName, int flags, int mode) throw()
{
	HANDLE hFile = INVALID_HANDLE_VALUE;

	try
	{
		std::string multiPath = ConvertPathToAbsoluteUnicode(pFileName);
		std::wstring widePath = multi2wide(multiPath);

		if (INVALID_HANDLE_VALUE == (hFile = CreateFileW(widePath.c_str(),
																		 dwDesiredAccess(flags),
																		 dwShareMode(flags),
																		 NULL,
																		 dwCreationDisposition(flags),
																		 dwFlagsAndAttributes(flags),
																		 NULL)))
		{
			throw Win32Exception(Win32Exception::API_CreateFile, pFileName);
		}

		if ((flags & O_APPEND) && INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_END))
			throw Win32Exception(Win32Exception::API_SetFilePointer, pFileName);

		return hFile;
	}
	EMU_EXCEPTION_HANDLING

	if (INVALID_HANDLE_VALUE != hFile)
		CloseHandle(hFile);

	return INVALID_HANDLE_VALUE;
}
