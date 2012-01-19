#include "Box.h"


using namespace Win32;


static time_t ConvertFileTimeToTime_t(FILETIME *fileTime) throw(Win32Exception)
{
	SYSTEMTIME stUTC;
	struct tm timeinfo;

	// Convert the last-write time to local time.
	if (!FileTimeToSystemTime(fileTime, &stUTC))
		throw Win32Exception(Win32Exception::API_FileTimeToSystemTime);

	memset(&timeinfo, 0, sizeof(timeinfo));
	timeinfo.tm_sec = stUTC.wSecond;
	timeinfo.tm_min = stUTC.wMinute;
	timeinfo.tm_hour = stUTC.wHour;
	timeinfo.tm_mday = stUTC.wDay;
	timeinfo.tm_wday = stUTC.wDayOfWeek;
	timeinfo.tm_mon = stUTC.wMonth - 1;
	// timeinfo.tm_yday = ...;
	timeinfo.tm_year = stUTC.wYear - 1900;

	time_t retVal = mktime(&timeinfo) - _timezone;
	return retVal;
}


static void hstat(HANDLE hFile, struct emu_stat_* st, std::string fileName = "") throw(Win32Exception)
{
	BY_HANDLE_FILE_INFORMATION fi;
	if (!GetFileInformationByHandle(hFile, &fi))
		throw Win32Exception(Win32Exception::API_GetFileInformationByHandle);
	/*
	if (INVALID_FILE_ATTRIBUTES == fi.dwFileAttributes)
	{
		::syslog(LOG_WARNING, "Failed to get file attributes: "
			"%s", GetErrorMessage(GetLastError()).c_str());
		errno = EACCES;
		return -1;
	}
	*/
	memset(st, 0, sizeof(*st));

	// This is how we get our INODE (equivalent) information
	ULARGE_INTEGER conv;
	conv.HighPart = fi.nFileIndexHigh;
	conv.LowPart  = fi.nFileIndexLow;
	st->st_ino = conv.QuadPart;

	// get the time information
	st->st_ctime = ConvertFileTimeToTime_t(&fi.ftCreationTime);
	st->st_atime = ConvertFileTimeToTime_t(&fi.ftLastAccessTime);
	st->st_mtime = ConvertFileTimeToTime_t(&fi.ftLastWriteTime);

	if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		st->st_size = 0;
	}
	else
	{
		conv.HighPart = fi.nFileSizeHigh;
		conv.LowPart  = fi.nFileSizeLow;
		st->st_size = conv.QuadPart;
	}

	// at the mo
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_nlink = 1;

	// the mode of the file
	// mode zero will make it impossible to restore on Unix
	// (no access to anybody, including the owner).
	// we'll fake a sensible mode:
	// all objects get user read (0400)
	// if it's a directory it gets user execute (0100)
	// if it's not read-only it gets user write (0200)
	st->st_mode = S_IREAD;

	if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		st->st_mode |= S_IFDIR | S_IEXEC;
	}
	else
	{
		st->st_mode |= S_IFREG;
	}

	if (!(fi.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
	{
		st->st_mode |= S_IWRITE;
	}

	// st_dev is normally zero, regardless of the drive letter,
	// since backup locations can't normally span drives. However,
	// a reparse point does allow all kinds of weird stuff to happen.
	// We set st_dev to 1 for a reparse point, so that Box will detect
	// a change of device number (from 0) and refuse to recurse down
	// the reparse point (which could lead to havoc).

	if (fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		st->st_dev = 1;
	}
	else
	{
		st->st_dev = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_stat
//		Purpose: replacement for the lstat and stat functions.
//			 Works with unicode filenames supplied in utf8.
//			 Returns a struct emu_stat to have room for 64-bit
//			 file identifier in st_ino (mingw allows only 16!)
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int emu_stat(const char * pName, struct emu_stat_ * st) throw()
{
	HANDLE hFile = INVALID_HANDLE_VALUE;

	try
	{
		hFile = OpenFileByNameUtf8(pName, FILE_READ_ATTRIBUTES | FILE_READ_EA);
		hstat(hFile, st, pName);
		CloseHandle(hFile);

		return 0;
	}
	EMU_EXCEPTION_HANDLING

	if (INVALID_HANDLE_VALUE != hFile)
		CloseHandle(hFile);

	return -1;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_fstat
//		Purpose: replacement for fstat. Supply a windows handle.
//			 Returns a struct emu_stat to have room for 64-bit
//			 file identifier in st_ino (mingw allows only 16!)
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int emu_fstat(HANDLE hdir, struct emu_stat_ * st)
{
	if (hdir == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_ERR, "Error: invalid file handle in emu_fstat()");
		errno = EBADF;
		return -1;
	}

	try
	{
		hstat(hdir,st);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}


int emu_mkdir(const char* pPathName, mode_t mode)
{
	try
	{
		std::string multiPath = ConvertPathToAbsoluteUnicode(pPathName);
		std::wstring widePath = multi2wide(multiPath);

		if (!CreateDirectoryW(widePath.c_str(),NULL))
			throw Win32Exception(Win32Exception::API_CreateDirectory);

		return 0;
	}
	EMU_EXCEPTION_HANDLING_RETURN(-1)
}
