#include "Box.h"


using namespace Win32;


static bool ConvertTime_tToFileTime(const time_t from, FILETIME *pTo)
{
	time_t adjusted = from + _timezone;
	struct tm *time_breakdown = gmtime(&adjusted);
	if (time_breakdown == NULL)
	{
		::syslog(LOG_ERR, "Error: failed to convert time format: "
			"%d is not a valid time\n", from);
		return false;
	}

	SYSTEMTIME stUTC;
	stUTC.wSecond       = static_cast<WORD>(time_breakdown->tm_sec);
	stUTC.wMinute       = static_cast<WORD>(time_breakdown->tm_min);
	stUTC.wHour         = static_cast<WORD>(time_breakdown->tm_hour);
	stUTC.wDay          = static_cast<WORD>(time_breakdown->tm_mday);
	stUTC.wDayOfWeek    = static_cast<WORD>(time_breakdown->tm_wday);
	stUTC.wMonth        = static_cast<WORD>(time_breakdown->tm_mon  + 1);
	stUTC.wYear         = static_cast<WORD>(time_breakdown->tm_year + 1900);
	stUTC.wMilliseconds = static_cast<WORD>(0);

	// Convert the last-write time to local time.
	if (!SystemTimeToFileTime(&stUTC, pTo))
		THROW_EXCEPTION_MESSAGE(Win32Exception, API_SystemTimeToFileTime, "Failed to convert between time formats")

	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_utimes
//		Purpose: replacement for the POSIX utimes() function,
//			works with unicode filenames supplied in utf8 format,
//			sets creation time instead of last access time.
//		Created: 25th July 2006
//
// --------------------------------------------------------------------------
int emu_utimes(const char * pName, const struct timeval times[])
{
	HANDLE	hFile	= INVALID_HANDLE_VALUE;

	try
	{
		FILETIME creationTime,
					modificationTime;

		ConvertTime_tToFileTime(times[0].tv_sec, &creationTime);
		ConvertTime_tToFileTime(times[1].tv_sec, &modificationTime);

		hFile = OpenFileByNameUtf8(pName, FILE_WRITE_ATTRIBUTES);

		if (!SetFileTime(hFile, &creationTime, NULL, &modificationTime))
			throw Win32Exception(Win32Exception::API_SetFileTime, pName);

		CloseHandle(hFile);

		return 0;
	}
	EMU_EXCEPTION_HANDLING

	if (INVALID_HANDLE_VALUE != hFile)
		CloseHandle(hFile);

	return -1;
}
