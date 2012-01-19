#ifndef EMU_INCLUDE
#	define EMU_INCLUDE

#	ifdef WIN32
#		pragma once

#		include "win32.h"
#		include "dirent.h"
#		include "getopt.h"
#		include "poll.h"
#		include "pwd.h"
#		include "unicode.h"
#		include "unistd.h"

#		include "_sys/mount.h"
#		include "_sys/resource.h"
#		include "_sys/socket.h"
#		include "_sys/stat.h"
#		include "_sys/syslog.h"
#		include "_sys/time.h"
#		include "_sys/uio.h"


		extern passwd gTempPasswd;


#		define EMU_EXCEPTION_HANDLING \
		catch(std::bad_alloc &e) \
		{ \
			(void)e; /* make the compiler happy */ \
			errno = ENOMEM; \
		} \
		catch(Win32Exception &e) \
		{ \
			BOX_LOG_WIN_ERROR_NUMBER(e.GetMessage(),e.GetLastError()); \
		} \
		catch(...) \
		{ \
			BOX_FATAL("Unexpected exception") \
		}

#		define EMU_EXCEPTION_HANDLING_RETURN(rv) \
		catch(std::bad_alloc &e) \
		{ \
			(void)e; /* make the compiler happy */ \
			errno = ENOMEM; \
		} \
		catch(Win32Exception &e) \
		{ \
			BOX_LOG_WIN_ERROR_NUMBER(e.GetMessage(),e.GetLastError()); \
		} \
		catch(...) \
		{ \
			BOX_FATAL("Unexpected exception") \
		} \
		return rv;



// basic types, may be required by other headers since we
// don't include sys/types.h


// emulated types, present on MinGW but not MSVC or vice versa

#ifdef __MINGW32__
	typedef uint32_t u_int32_t;
#else
#endif

// emulated functions

#define gmtime_r( _clock, _result ) \
	( *(_result) = *gmtime( (_clock) ), \
	(_result) )

#define ITIMER_REAL 0






#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#define timespec timeval

//win32 deals in usec not nsec - so need to ensure this follows through
#define tv_nsec tv_usec 

#define S_IRGRP S_IWRITE
#define S_IWGRP S_IREAD
#define S_IROTH S_IWRITE | S_IREAD
#define S_IWOTH S_IREAD | S_IREAD

//again need to verify these
#define S_IFLNK 1
#define S_IFSOCK 0
#define S_IFIFO 0

#define S_ISLNK(x) ( false )

#define vsnprintf _vsnprintf

#ifndef __MINGW32__
inline int strcasecmp(const char *s1, const char *s2)
{
	return _stricmp(s1,s2);
}
#endif

#ifdef _DIRENT_H_
#error You must not include the MinGW dirent.h!
#endif

// local constant to open file exclusively without shared access
#define O_LOCK 0x10000

HANDLE openfile(const char *filename, int flags, int mode);
inline int closefile(HANDLE handle)
{
	if (CloseHandle(handle) != TRUE)
	{
		errno = EINVAL;
		return -1;
	}
	return 0;
}


#ifndef __MINGW32__
#define strtoll _strtoi64
#endif





// The following functions are not emulations, but utilities for other 
// parts of the code where Windows API is used or windows-specific stuff 
// is needed, like codepage conversion.

bool EnableBackupRights( void );

// Utility function which returns a default config file name,
// based on the path of the current executable.
std::string GetDefaultConfigFilePath(const std::string& rName);

// console_read() is a replacement for _cgetws which requires a 
// relatively recent C runtime lib
int console_read(char* pBuffer, size_t BufferSize);

#endif // !EMU_INCLUDE && WIN32

#endif
