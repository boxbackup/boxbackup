// Box Backup Win32 native port by Nick Knight

// Need at least 0x0500 to use GetFileSizeEx on Cygwin/MinGW
#define WINVER 0x0500

#include "Box.h"

#ifdef WIN32

// #include "emu.h"

#include <windows.h>
#include <fcntl.h>
// #include <atlenc.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif
#ifdef HAVE_PROCESS_H
	#include <process.h>
#endif

#include <string>
#include <list>

// our implementation for a timer, based on a 
// simple thread which sleeps for a period of time

static bool gFinishTimer;
static CRITICAL_SECTION gLock;

typedef struct 
{
	int countDown;
	int interval;
}
Timer_t;

std::list<Timer_t> gTimerList;
static void (__cdecl *gTimerFunc) (int) = NULL;

int setitimer(int type, struct itimerval *timeout, void *arg)
{
	if (ITIMER_VIRTUAL == type)
	{
		EnterCriticalSection(&gLock);
		// we only need seconds for the mo!
		if (timeout->it_value.tv_sec  == 0 && 
		    timeout->it_value.tv_usec == 0)
		{
			gTimerList.clear();
		}
		else
		{
			Timer_t ourTimer;
			ourTimer.countDown = timeout->it_value.tv_sec;
			ourTimer.interval  = timeout->it_interval.tv_sec;
			gTimerList.push_back(ourTimer);
		}
		LeaveCriticalSection(&gLock);
	}
	
	// indicate success
	return 0;
}

static unsigned int WINAPI RunTimer(LPVOID lpParameter)
{
	gFinishTimer = false;

	while (!gFinishTimer)
	{
		std::list<Timer_t>::iterator it;
		EnterCriticalSection(&gLock);

		for (it = gTimerList.begin(); it != gTimerList.end(); it++)
		{
			Timer_t& rTimer(*it);

			rTimer.countDown --;
			if (rTimer.countDown == 0)
			{
				if (gTimerFunc != NULL)
				{
					gTimerFunc(0);
				}
				if (rTimer.interval)
				{
					rTimer.countDown = rTimer.interval;
				}
				else
				{
					// mark for deletion
					rTimer.countDown = -1;
				}
			}
		}

		for (it = gTimerList.begin(); it != gTimerList.end(); it++)
		{
			Timer_t& rTimer(*it);

			if (rTimer.countDown == -1)
			{
				gTimerList.erase(it);
				
				// the iterator is now invalid, so restart search
				it = gTimerList.begin();

				// if the list is now empty, don't try to increment 
				// the iterator again
				if (it == gTimerList.end()) break;
			}
		}

		LeaveCriticalSection(&gLock);
		// we only need to have a 1 second resolution
		Sleep(1000);
	}

	return 0;
}

int SetTimerHandler(void (__cdecl *func ) (int))
{
	gTimerFunc = func;
	return 0;
}

void InitTimer(void)
{
	InitializeCriticalSection(&gLock);

	// create our thread
	HANDLE ourThread = (HANDLE)_beginthreadex(NULL, 0, RunTimer, 0, 
		CREATE_SUSPENDED, NULL);
	SetThreadPriority(ourThread, THREAD_PRIORITY_LOWEST);
	ResumeThread(ourThread);
}

void FiniTimer(void)
{
	gFinishTimer = true;
	EnterCriticalSection(&gLock);
	DeleteCriticalSection(&gLock);
}

//Our constants we need to keep track of
//globals
struct passwd gTempPasswd;

bool EnableBackupRights( void )
{
	HANDLE hToken;
	TOKEN_PRIVILEGES token_priv;

	//open current process to adjust privileges
	if( !OpenProcessToken( GetCurrentProcess( ), 
		TOKEN_ADJUST_PRIVILEGES, 
		&hToken ))
	{
		printf( "Cannot open process token: error %d\n", 
			(int)GetLastError() );
		return false;
	}

	//let's build the token privilege struct - 
	//first, look up the LUID for the backup privilege

	if( !LookupPrivilegeValue( NULL, //this system
		SE_BACKUP_NAME, //the name of the privilege
		&( token_priv.Privileges[0].Luid )) ) //result
	{
		printf( "Cannot lookup backup privilege: error %d\n", 
			(int)GetLastError( ) );
		return false;
	}

	token_priv.PrivilegeCount = 1;
	token_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// now set the privilege
	// because we're going exit right after dumping the streams, there isn't 
	// any need to save current state

	if( !AdjustTokenPrivileges( hToken, //our process token
		false,  //we're not disabling everything
		&token_priv, //address of structure
		sizeof( token_priv ), //size of structure
		NULL, NULL )) //don't save current state
	{
		//this function is a little tricky - if we were adjusting
		//more than one privilege, it could return success but not
		//adjust them all - in the general case, you need to trap this
		printf( "Could not enable backup privileges: error %d\n", 
			(int)GetLastError( ) );
		return false;

	}
	else
	{
		return true;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertToWideString
//		Purpose: Converts a string from specified codepage to 
//			 a wide string (WCHAR*). Returns a buffer which 
//			 MUST be freed by the caller with delete[].
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
WCHAR* ConvertToWideString(const char* pString, unsigned int codepage)
{
	int len = MultiByteToWideChar
	(
		codepage, // source code page
		0,        // character-type options
		pString,  // string to map
		-1,       // number of bytes in string - auto detect
		NULL,     // wide-character buffer
		0         // size of buffer - work out 
		          //   how much space we need
	);

	if (len == 0)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert string to wide string: "
			"error %d", GetLastError());
		errno = EINVAL;
		return NULL;
	}

	WCHAR* buffer = new WCHAR[len];

	if (buffer == NULL)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert string to wide string: "
			"out of memory");
		errno = ENOMEM;
		return NULL;
	}

	len = MultiByteToWideChar
	(
		codepage, // source code page
		0,        // character-type options
		pString,  // string to map
		-1,       // number of bytes in string - auto detect
		buffer,   // wide-character buffer
		len       // size of buffer
	);

	if (len == 0)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert string to wide string: "
			"error %i", GetLastError());
		errno = EACCES;
		delete [] buffer;
		return NULL;
	}

	return buffer;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertUtf8ToWideString
//		Purpose: Converts a string from UTF-8 to a wide string.
//			 Returns a buffer which MUST be freed by the caller 
//			 with delete[].
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
WCHAR* ConvertUtf8ToWideString(const char* pString)
{
	return ConvertToWideString(pString, CP_UTF8);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertFromWideString
//		Purpose: Converts a wide string to a narrow string in the
//			 specified code page. Returns a buffer which MUST 
//			 be freed by the caller with delete[].
//			 In case of fire, logs the error and returns NULL.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
char* ConvertFromWideString(const WCHAR* pString, unsigned int codepage)
{
	int len = WideCharToMultiByte
	(
		codepage, // destination code page
		0,        // character-type options
		pString,  // string to map
		-1,       // number of bytes in string - auto detect
		NULL,     // output buffer
		0,        // size of buffer - work out 
		          //   how much space we need
		NULL,     // replace unknown chars with system default
		NULL      // don't tell us when that happened
	);

	if (len == 0)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert wide string to narrow: "
			"error %d", GetLastError());
		errno = EINVAL;
		return NULL;
	}

	char* buffer = new char[len];

	if (buffer == NULL)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert wide string to narrow: "
			"out of memory");
		errno = ENOMEM;
		return NULL;
	}

	len = WideCharToMultiByte
	(
		codepage, // source code page
		0,        // character-type options
		pString,  // string to map
		-1,       // number of bytes in string - auto detect
		buffer,   // output buffer
		len,      // size of buffer
		NULL,     // replace unknown chars with system default
		NULL      // don't tell us when that happened
	);

	if (len == 0)
	{
		::syslog(LOG_WARNING, 
			"Failed to convert wide string to narrow: "
			"error %i", GetLastError());
		errno = EACCES;
		delete [] buffer;
		return NULL;
	}

	return buffer;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertUtf8ToConsole
//		Purpose: Converts a string from UTF-8 to the console 
//			 code page. On success, replaces contents of rDest 
//			 and returns true. In case of fire, logs the error 
//			 and returns false.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
bool ConvertUtf8ToConsole(const char* pString, std::string& rDest)
{
	WCHAR* pWide = ConvertToWideString(pString, CP_UTF8);
	if (pWide == NULL)
	{
		return false;
	}

	char* pConsole = ConvertFromWideString(pWide, GetConsoleOutputCP());
	delete [] pWide;

	if (!pConsole)
	{
		return false;
	}

	rDest = pConsole;
	delete [] pConsole;

	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertConsoleToUtf8
//		Purpose: Converts a string from the console code page
//			 to UTF-8. On success, replaces contents of rDest
//			 and returns true. In case of fire, logs the error 
//			 and returns false.
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
bool ConvertConsoleToUtf8(const char* pString, std::string& rDest)
{
	WCHAR* pWide = ConvertToWideString(pString, GetConsoleCP());
	if (pWide == NULL)
	{
		return false;
	}

	char* pConsole = ConvertFromWideString(pWide, CP_UTF8);
	delete [] pWide;

	if (!pConsole)
	{
		return false;
	}

	rDest = pConsole;
	delete [] pConsole;

	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ConvertPathToAbsoluteUnicode
//		Purpose: Converts relative paths to absolute (with unicode marker)
//		Created: 4th February 2006
//
// --------------------------------------------------------------------------
std::string ConvertPathToAbsoluteUnicode(const char *pFileName)
{
	std::string tmpStr("\\\\?\\");
	
	// Is the path relative or absolute?
	// Absolute paths on Windows are always a drive letter
	// followed by ':'
	
	if (pFileName[1] != ':')
	{
		// Must be relative. We need to get the 
		// current directory to make it absolute.
		
		char wd[PATH_MAX];
		if (::getcwd(wd, PATH_MAX) == 0)
		{
			::syslog(LOG_WARNING, 
				"Failed to open '%s': path too long", 
				pFileName);
			errno = ENAMETOOLONG;
			tmpStr = "";
			return tmpStr;
		}
		
		tmpStr += wd;
		if (tmpStr[tmpStr.length()] != '\\')
		{
			tmpStr += '\\';
		}
	}
	
	tmpStr += pFileName;
	return tmpStr;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    openfile
//		Purpose: replacement for any open calls - handles unicode filenames - supplied in utf8
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
HANDLE openfile(const char *pFileName, int flags, int mode)
{
	std::string AbsPathWithUnicode = ConvertPathToAbsoluteUnicode(pFileName);
	
	if (AbsPathWithUnicode.size() == 0)
	{
		// error already logged by ConvertPathToAbsoluteUnicode()
		return NULL;
	}
	
	WCHAR* pBuffer = ConvertUtf8ToWideString(AbsPathWithUnicode.c_str());
	// We are responsible for freeing pBuffer
	
	if (pBuffer == NULL)
	{
		// error already logged by ConvertUtf8ToWideString()
		return NULL;
	}

	// flags could be O_WRONLY | O_CREAT | O_RDONLY
	DWORD createDisposition = OPEN_EXISTING;
	DWORD shareMode = FILE_SHARE_READ;
	DWORD accessRights = FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_EA;

	if (flags & O_WRONLY)
	{
		shareMode = FILE_SHARE_WRITE;
	}
	if (flags & O_RDWR)
	{
		shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	}
	if (flags & O_CREAT)
	{
		createDisposition = OPEN_ALWAYS;
		shareMode |= FILE_SHARE_WRITE;
		accessRights |= FILE_WRITE_ATTRIBUTES 
			| FILE_WRITE_DATA | FILE_WRITE_EA 
			| FILE_ALL_ACCESS;
	}
	if (flags & O_TRUNC)
	{
		createDisposition = CREATE_ALWAYS;
	}
	if (flags & O_EXCL)
	{
		shareMode = 0;
	}

	HANDLE hdir = CreateFileW(pBuffer, 
		accessRights, 
		shareMode, 
		NULL, 
		createDisposition, 
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);
	
	delete [] pBuffer;

	if (hdir == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_WARNING, "Failed to open file %s: "
			"error %i", pFileName, GetLastError());
		return NULL;
	}

	return hdir;
}

// MinGW provides a getopt implementation
#ifndef __MINGW32__
//works with getopt
char *optarg;
//optind looks like an index into the string - how far we have moved along
int optind = 1;
char nextchar = -1;
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_fstat
//		Purpose: replacement for fstat supply a windows handle
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int emu_fstat(HANDLE hdir, struct stat * st)
{
	ULARGE_INTEGER conv;

	if (hdir == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_ERR, "Error: invalid file handle in emu_fstat()");
		errno = EBADF;
		return -1;
	}

	BY_HANDLE_FILE_INFORMATION fi;
	if (!GetFileInformationByHandle(hdir, &fi))
	{
		::syslog(LOG_WARNING, "Failed to read file information: "
			"error %d", GetLastError());
		errno = EACCES;
		return -1;
	}

	if (INVALID_FILE_ATTRIBUTES == fi.dwFileAttributes)
	{
		::syslog(LOG_WARNING, "Failed to get file attributes: "
			"error %d", GetLastError());
		errno = EACCES;
		return -1;
	}

	memset(st, 0, sizeof(*st));

	// This next example is how we get our INODE (equivalent) information
	conv.HighPart = fi.nFileIndexHigh;
	conv.LowPart = fi.nFileIndexLow;
	st->st_ino = (_ino_t)conv.QuadPart;

	// get the time information
	st->st_ctime = ConvertFileTimeToTime_t(&fi.ftCreationTime);
	st->st_atime = ConvertFileTimeToTime_t(&fi.ftLastAccessTime);
	st->st_mtime = ConvertFileTimeToTime_t(&fi.ftLastWriteTime);

	// size of the file
	LARGE_INTEGER st_size;
	if (!GetFileSizeEx(hdir, &st_size))
	{
		::syslog(LOG_WARNING, "Failed to get file size: error %d",
			GetLastError());
		errno = EACCES;
		return -1;
	}

	conv.HighPart = st_size.HighPart;
	conv.LowPart = st_size.LowPart;
	st->st_size = (_off_t)conv.QuadPart;

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
	st->st_mode = 0400;

	if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		st->st_mode |= S_IFDIR | 0100;
	}
	else
	{
		st->st_mode |= S_IFREG;
	}

	if (!(fi.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
	{
		st->st_mode |= 0200;
	}

	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    OpenFileByNameUtf8
//		Purpose: Converts filename to Unicode and returns 
//			a handle to it. In case of error, sets errno,
//			logs the error and returns NULL.
//		Created: 10th December 2004
//
// --------------------------------------------------------------------------
HANDLE OpenFileByNameUtf8(const char* pFileName)
{
	std::string AbsPathWithUnicode = ConvertPathToAbsoluteUnicode(pFileName);
	
	if (AbsPathWithUnicode.size() == 0)
	{
		// error already logged by ConvertPathToAbsoluteUnicode()
		return NULL;
	}
	
	WCHAR* pBuffer = ConvertUtf8ToWideString(AbsPathWithUnicode.c_str());
	// We are responsible for freeing pBuffer
	
	if (pBuffer == NULL)
	{
		// error already logged by ConvertUtf8ToWideString()
		return NULL;
	}

	HANDLE handle = CreateFileW(pBuffer, 
		FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_EA, 
		FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, 
		NULL, 
		OPEN_EXISTING, 
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	if (handle == INVALID_HANDLE_VALUE)
	{
		// if our open fails we should always be able to 
		// open in this mode - to get the inode information
		// at least one process must have the file open - 
		// in this case someone else does.
		handle = CreateFileW(pBuffer, 
			0, 
			FILE_SHARE_READ, 
			NULL, 
			OPEN_EXISTING, 
			FILE_FLAG_BACKUP_SEMANTICS,
			NULL);
	}

	delete [] pBuffer;

	if (handle == INVALID_HANDLE_VALUE)
	{
		DWORD err = GetLastError();

		if (err == ERROR_FILE_NOT_FOUND)
		{
			errno = ENOENT;
		}
		else
		{
			::syslog(LOG_WARNING, 
				"Failed to open '%s': error %d", pFileName, err);
			errno = EACCES;
		}

		return NULL;
	}

	return handle;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    emu_stat 
//		Purpose: replacement for the lstat and stat functions, 
//			works with unicode filenames supplied in utf8 format
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int emu_stat(const char * pName, struct stat * st)
{
	HANDLE handle = OpenFileByNameUtf8(pName);

	if (handle == NULL)
	{
		// errno already set and error logged by OpenFileByNameUtf8()
		return -1;
	}

	int retVal = emu_fstat(handle, st);
	if (retVal != 0)
	{
		// error logged, but without filename
		::syslog(LOG_WARNING, "Failed to get file information "
			"for '%s'", pName);
	}

	// close the handle
	CloseHandle(handle);

	return retVal;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    statfs
//		Purpose: returns the mount point of where a file is located - 
//			in this case the volume serial number
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int statfs(const char * pName, struct statfs * s)
{
	HANDLE handle = OpenFileByNameUtf8(pName);

	if (handle == NULL)
	{
		// errno already set and error logged by OpenFileByNameUtf8()
		return -1;
	}

	BY_HANDLE_FILE_INFORMATION fi;
	if (!GetFileInformationByHandle(handle, &fi))
	{
		::syslog(LOG_WARNING, "Failed to get file information "
			"for '%s': error %d", pName, GetLastError());
		CloseHandle(handle);
		errno = EACCES;
		return -1;
	}

	// convert volume serial number to a string
	_ui64toa(fi.dwVolumeSerialNumber, s->f_mntonname + 1, 16);

	// pseudo unix mount point
	s->f_mntonname[0] = DIRECTORY_SEPARATOR_ASCHAR;

	CloseHandle(handle);   // close the handle

	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    opendir
//		Purpose: replacement for unix function, uses win32 findfirst routines
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
DIR *opendir(const char *name)
{
	if (!name || !name[0])
	{
		errno = EINVAL;
		return NULL;
	}
	
	std::string dirName(name);

	//append a '\' win32 findfirst is sensitive to this
	if ( dirName[dirName.size()] != '\\' || dirName[dirName.size()] != '/' )
	{
		dirName += '\\';
	}

	// what is the search string? - everything
	dirName += '*';

	DIR *pDir = new DIR;
	if (pDir == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}

	pDir->name = ConvertUtf8ToWideString(dirName.c_str());
	// We are responsible for freeing dir->name
	
	if (pDir->name == NULL)
	{
		delete pDir;
		return NULL;
	}

	pDir->fd = _wfindfirst((const wchar_t*)pDir->name, &(pDir->info));

	if (pDir->fd == -1)
	{
		delete [] pDir->name;
		delete pDir;
		return NULL;
	}
		
	pDir->result.d_name = 0;
	return pDir;
}

// this kinda makes it not thread friendly!
// but I don't think it needs to be.
char tempbuff[MAX_PATH];

// --------------------------------------------------------------------------
//
// Function
//		Name:    readdir
//		Purpose: as function above
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
struct dirent *readdir(DIR *dp)
{
	try
	{
		struct dirent *den = NULL;

		if (dp && dp->fd != -1)
		{
			if (!dp->result.d_name || 
				_wfindnext(dp->fd, &dp->info) != -1)
			{
				den         = &dp->result;
				std::wstring input(dp->info.name);
				memset(tempbuff, 0, sizeof(tempbuff));
				WideCharToMultiByte(CP_UTF8, 0, dp->info.name, 
					-1, &tempbuff[0], sizeof (tempbuff), 
					NULL, NULL);
				//den->d_name = (char *)dp->info.name;
				den->d_name = &tempbuff[0];
			}
		}
		else
		{
			errno = EBADF;
		}
		return den;
	}
	catch (...)
	{
		printf("Caught readdir");
	}
	return NULL;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    closedir
//		Purpose: as function above
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int closedir(DIR *dp)
{
	try
	{
		int finres = -1;
		if (dp)
		{
			if(dp->fd != -1)
			{
				finres = _findclose(dp->fd);
			}

			delete [] dp->name;
			delete dp;
		}

		if (finres == -1) // errors go to EBADF 
		{
			errno = EBADF;
		}

		return finres;
	}
	catch (...)
	{
		printf("Caught closedir");
	}
	return -1;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    poll
//		Purpose: a weak implimentation (just enough for box) 
//			of the unix poll for winsock2
//		Created: 25th October 2004
//
// --------------------------------------------------------------------------
int poll (struct pollfd *ufds, unsigned long nfds, int timeout)
{
	try
	{
		fd_set readfd;
		fd_set writefd;

		readfd.fd_count = 0;
		writefd.fd_count = 0;

		struct pollfd *ufdsTmp = ufds;

		timeval timOut;
		timeval *tmpptr; 

		if (timeout == INFTIM)
			tmpptr = NULL;
		else
			tmpptr = &timOut;

		timOut.tv_sec  = timeout / 1000;
		timOut.tv_usec = timeout * 1000;

		if (ufds->events & POLLIN)
		{
			for (unsigned long i = 0; i < nfds; i++)
			{
				readfd.fd_array[i] = ufdsTmp->fd;
				readfd.fd_count++;
			}
		}

		if (ufds->events & POLLOUT)
		{
			for (unsigned long i = 0; i < nfds; i++)
			{

				writefd.fd_array[i]=ufdsTmp->fd;
				writefd.fd_count++;
			}
		}	

		int noffds = select(0, &readfd, &writefd, 0, tmpptr);

		if (noffds == SOCKET_ERROR)
		{
			// int errval = WSAGetLastError();

			ufdsTmp = ufds;
			for (unsigned long i = 0; i < nfds; i++)
			{
				ufdsTmp->revents = POLLERR;
				ufdsTmp++;
			}
			return (-1);
		}

		return noffds;
	}
	catch (...)
	{
		printf("Caught poll");
	}

	return -1;
}

HANDLE gSyslogH = 0;
static bool sHaveWarnedEventLogFull = false;

void syslog(int loglevel, const char *frmt, ...)
{
	WORD errinfo;
	char buffer[1024];
	std::string sixfour(frmt);

	switch (loglevel)
	{
	case LOG_INFO:
		errinfo = EVENTLOG_INFORMATION_TYPE;
		break;
	case LOG_ERR:
		errinfo = EVENTLOG_ERROR_TYPE;
		break;
	case LOG_WARNING:
		errinfo = EVENTLOG_WARNING_TYPE;
		break;
	default:
		errinfo = EVENTLOG_WARNING_TYPE;
		break;
	}

	// taken from MSDN
	int sixfourpos;
	while ( (sixfourpos = (int)sixfour.find("%ll")) != -1 )
	{
		// maintain portability - change the 64 bit formater...
		std::string temp = sixfour.substr(0,sixfourpos);
		temp += "%I64";
		temp += sixfour.substr(sixfourpos+3, sixfour.length());
		sixfour = temp;
	}

	// printf("parsed string is:%s\r\n", sixfour.c_str());

	va_list args;
	va_start(args, frmt);

	int len = vsnprintf(buffer, sizeof(buffer)-1, sixfour.c_str(), args);
	ASSERT(len < sizeof(buffer))
	buffer[sizeof(buffer)-1] = 0;

	va_end(args);

	LPCSTR strings[] = { buffer, NULL };

	if (!ReportEvent(gSyslogH, // event log handle 
		errinfo,               // event type 
		0,                     // category zero 
		MSG_ERR_EXIST,	       // event identifier - 
		                       // we will call them all the same
		NULL,                  // no user security identifier 
		1,                     // one substitution string 
		0,                     // no data 
		strings,               // pointer to string array 
		NULL))                 // pointer to data 

	{
		DWORD err = GetLastError();
		if (err == ERROR_LOG_FILE_FULL)
		{
			if (!sHaveWarnedEventLogFull)
			{
				printf("Unable to send message to Event Log "
					"(Event Log is full):\r\n");
				sHaveWarnedEventLogFull = TRUE;
			}
		}
		else
		{
			printf("Unable to send message to Event Log: "
				"error %i:\r\n", (int)err);
		}
	}
	else
	{
		sHaveWarnedEventLogFull = false;
	}

	printf("%s\r\n", buffer);
}

int emu_chdir(const char* pDirName)
{
	WCHAR* pBuffer = ConvertUtf8ToWideString(pDirName);
	if (!pBuffer) return -1;
	int result = SetCurrentDirectoryW(pBuffer);
	delete [] pBuffer;
	if (result != 0) return 0;
	errno = EACCES;
	return -1;
}

char* emu_getcwd(char* pBuffer, int BufSize)
{
	DWORD len = GetCurrentDirectoryW(0, NULL);
	if (len == 0)
	{
		errno = EINVAL;
		return NULL;
	}

	if (len > BufSize)
	{
		errno = ENAMETOOLONG;
		return NULL;
	}

	WCHAR* pWide = new WCHAR [len];
	if (!pWide)
	{
		errno = ENOMEM;
		return NULL;
	}

	DWORD result = GetCurrentDirectoryW(len, pWide);
	if (result <= 0 || result >= len)
	{
		errno = EACCES;
		return NULL;
	}

	char* pUtf8 = ConvertFromWideString(pWide, CP_UTF8);
	delete [] pWide;

	if (!pUtf8)
	{
		return NULL;
	}

	strncpy(pBuffer, pUtf8, BufSize - 1);
	pBuffer[BufSize - 1] = 0;
	delete [] pUtf8;

	return pBuffer;
}

int emu_mkdir(const char* pPathName)
{
	WCHAR* pBuffer = ConvertToWideString(pPathName, CP_UTF8);
	if (!pBuffer)
	{
		return -1;
	}

	BOOL result = CreateDirectoryW(pBuffer, NULL);
	delete [] pBuffer;

	if (!result)
	{
		errno = EACCES;
		return -1;
	}

	return 0;
}

int emu_unlink(const char* pFileName)
{
	WCHAR* pBuffer = ConvertToWideString(pFileName, CP_UTF8);
	if (!pBuffer)
	{
		return -1;
	}

	BOOL result = DeleteFileW(pBuffer);
	delete [] pBuffer;

	if (!result)
	{
		errno = EACCES;
		return -1;
	}

	return 0;
}

int console_read(char* pBuffer, size_t BufferSize)
{
	HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);

	if (hConsole == INVALID_HANDLE_VALUE)
	{
		::fprintf(stderr, "Failed to get a handle on standard input: "
			"error %d\n", GetLastError());
		return -1;
	}

	int WideSize = BufferSize / 5;
	WCHAR* pWideBuffer = new WCHAR [WideSize];

	if (!pWideBuffer)
	{
		::perror("Failed to allocate wide character buffer");
		return -1;
	}

	DWORD numCharsRead = 0;

	if (!ReadConsoleW(
			hConsole,
			pWideBuffer,
			WideSize - 1,
			&numCharsRead,
			NULL // reserved
		)) 
	{
		::fprintf(stderr, "Failed to read from console: error %d\n",
			GetLastError());
		return -1;
	}

	pWideBuffer[numCharsRead] = 0;

	char* pUtf8 = ConvertFromWideString(pWideBuffer, GetConsoleCP());
	strncpy(pBuffer, pUtf8, BufferSize);
	delete [] pUtf8;

	return strlen(pBuffer);
}

#endif // WIN32
