// emulates unix syscalls to win32 functions

#ifdef WIN32
	#define EMU_STRUCT_STAT struct emu_stat
	#define EMU_STAT  emu_stat
	#define EMU_FSTAT emu_fstat
	#define EMU_LSTAT emu_stat
#else
	#define EMU_STRUCT_STAT struct stat
	#define EMU_STAT  ::stat
	#define EMU_FSTAT ::fstat
	#define EMU_LSTAT ::lstat
#endif

#if ! defined EMU_INCLUDE && defined WIN32
#define EMU_INCLUDE

// basic types, may be required by other headers since we
// don't include sys/types.h

#ifdef __MINGW32__
	#include <stdint.h>
#else // MSVC
	typedef unsigned __int64 u_int64_t;
	typedef unsigned __int64 uint64_t;
	typedef          __int64 int64_t;
	typedef unsigned __int32 uint32_t;
	typedef unsigned __int32 u_int32_t;
	typedef          __int32 int32_t;
	typedef unsigned __int16 uint16_t;
	typedef          __int16 int16_t;
	typedef unsigned __int8  uint8_t;
	typedef          __int8  int8_t;
#endif

// emulated types, present on MinGW but not MSVC or vice versa

#ifdef __MINGW32__
	typedef uint32_t u_int32_t;
#else
	typedef unsigned int mode_t;
	typedef unsigned int pid_t;
#endif

// set up to include the necessary parts of Windows headers

#define WIN32_LEAN_AND_MEAN

#ifndef __MSVCRT_VERSION__
#define __MSVCRT_VERSION__ 0x0601
#endif

// Windows headers

#include <winsock2.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include <string>

// emulated functions

#define gmtime_r( _clock, _result ) \
	( *(_result) = *gmtime( (_clock) ), \
	(_result) )

#define ITIMER_REAL 0

#ifdef _MSC_VER
// Microsoft decided to deprecate the standard POSIX functions. Great!
#define open(file,flags,mode) _open(file,flags,mode)
#define close(fd)             _close(fd)
#define dup(fd)               _dup(fd)
#define read(fd,buf,count)    _read(fd,buf,count)
#define write(fd,buf,count)   _write(fd,buf,count)
#define lseek(fd,off,whence)  _lseek(fd,off,whence)
#define fileno(struct_file)   _fileno(struct_file)
#endif

struct passwd {
	char *pw_name;
	char *pw_passwd;
	int pw_uid;
	int pw_gid;
	time_t pw_change;
	char *pw_class;
	char *pw_gecos;
	char *pw_dir;
	char *pw_shell;
	time_t pw_expire;
};

extern passwd gTempPasswd;
inline struct passwd * getpwnam(const char * name)
{
	//for the mo pretend to be root
	gTempPasswd.pw_uid = 0;
	gTempPasswd.pw_gid = 0;

	return &gTempPasswd;
}

#define S_IRWXG 1
#define S_IRWXO 2
#define S_ISUID 4
#define S_ISGID 8
#define S_ISVTX 16

#ifndef __MINGW32__
	//not sure if these are correct
	//S_IWRITE -   writing permitted
	//_S_IREAD -    reading permitted
	//_S_IREAD | _S_IWRITE - 
	#define S_IRUSR S_IWRITE
	#define S_IWUSR S_IREAD
	#define S_IRWXU (S_IREAD|S_IWRITE|S_IEXEC)

	#define S_ISREG(x) (S_IFREG & x)
	#define S_ISDIR(x) (S_IFDIR & x)
#endif

inline int chown(const char * Filename, u_int32_t uid, u_int32_t gid)
{
	//important - this needs implementing
	//If a large restore is required then 
	//it needs to restore files AND permissions
	//reference AdjustTokenPrivileges
	//GetAccountSid
	//InitializeSecurityDescriptor
	//SetSecurityDescriptorOwner
	//The next function looks like the guy to use...
	//SetFileSecurity

	//indicate success
	return 0;
}

// Windows and Unix owners and groups are pretty fundamentally different.
// Ben prefers that we kludge here rather than litter the code with #ifdefs.
// Pretend to be root, and pretend that set...() operations succeed.
inline int setegid(int)
{
	return true;
}
inline int seteuid(int)
{
	return true;
}
inline int setgid(int)
{
	return true;
}
inline int setuid(int)
{
	return true;
}
inline int getgid(void)
{
	return 0;
}
inline int getuid(void)
{
	return 0;
}
inline int geteuid(void)
{
	return 0;
}

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

// MinGW provides a getopt implementation
#ifndef __MINGW32__
#include "getopt.h"
#endif // !__MINGW32__

#define timespec timeval

//win32 deals in usec not nsec - so need to ensure this follows through
#define tv_nsec tv_usec 

#ifndef __MINGW32__
	typedef int socklen_t;
#endif

#define S_IRGRP S_IWRITE
#define S_IWGRP S_IREAD
#define S_IROTH S_IWRITE | S_IREAD
#define S_IWOTH S_IREAD | S_IREAD

//again need to verify these
#define S_IFLNK 1
#define S_IFSOCK 0

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

struct dirent
{
	char *d_name;
	unsigned long d_type;
};

struct DIR
{
	intptr_t		fd;	// filedescriptor
	// struct _finddata_t	info;
	struct _wfinddata_t	info;
	// struct _finddata_t 	info;
	struct dirent		result;	// d_name (first time null)
	wchar_t			*name;	// null-terminated byte string
};

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dp);
int closedir(DIR *dp);

// local constant to open file exclusively without shared access
#define O_LOCK 0x10000

extern DWORD winerrno; /* used to report errors from openfile() */
HANDLE openfile(const char *filename, int flags, int mode);

#define LOG_DEBUG LOG_INFO
#define LOG_INFO 6
#define LOG_NOTICE LOG_INFO
#define LOG_WARNING 4
#define LOG_ERR 3
#define LOG_CRIT LOG_ERR
#define LOG_PID 0
#define LOG_LOCAL5 0
#define LOG_LOCAL6 0

void openlog (const char * daemonName, int, int);
void closelog(void);
void syslog  (int loglevel, const char *fmt, ...);

#define LOG_LOCAL0 0
#define LOG_LOCAL1 0
#define LOG_LOCAL2 0
#define LOG_LOCAL3 0
#define LOG_LOCAL4 0
#define LOG_LOCAL5 0
#define LOG_LOCAL6 0
#define LOG_DAEMON 0

#ifndef __MINGW32__
#define strtoll _strtoi64
#endif

inline unsigned int sleep(unsigned int secs)
{
	Sleep(secs*1000);
	return(ERROR_SUCCESS);
}

#define INFTIM -1
#define POLLIN 0x1
#define POLLERR 0x8
#define POLLOUT 0x4

#define SHUT_RDWR SD_BOTH
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND

struct pollfd
{
	SOCKET fd;
	short int events;
	short int revents;
};

inline int ioctl(SOCKET sock, int flag,  int * something)
{
	//indicate success
	return 0;
}

extern "C" inline int getpid()
{
	return (int)GetCurrentProcessId();
}

inline int waitpid(pid_t pid, int *status, int)
{
	return 0;
}

//this shouldn't be needed.
struct statfs
{
	TCHAR f_mntonname[MAX_PATH];
};

struct emu_stat {
	int st_dev;
	uint64_t st_ino;
	DWORD st_mode;
	short st_nlink;
	short st_uid;
	short st_gid;
	//_dev_t st_rdev;
	uint64_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
};

// need this for conversions
time_t ConvertFileTimeToTime_t(FILETIME *fileTime);
bool   ConvertTime_tToFileTime(const time_t from, FILETIME *pTo);

int   emu_chdir  (const char* pDirName);
int   emu_mkdir  (const char* pPathName);
int   emu_unlink (const char* pFileName);
int   emu_fstat  (HANDLE file,       struct emu_stat* st);
int   emu_stat   (const char* pName, struct emu_stat* st);
int   emu_utimes (const char* pName, const struct timeval[]);
int   emu_chmod  (const char* pName, mode_t mode);
char* emu_getcwd (char* pBuffer,     int BufSize);
int   emu_rename (const char* pOldName, const char* pNewName);

#define chdir(directory)        emu_chdir  (directory)
#define mkdir(path,     mode)   emu_mkdir  (path)
#define unlink(file)            emu_unlink (file)
#define utimes(buffer,  times)  emu_utimes (buffer,   times)
#define chmod(file,     mode)   emu_chmod  (file,     mode)
#define getcwd(buffer,  size)   emu_getcwd (buffer,   size)
#define rename(oldname, newname) emu_rename (oldname, newname)

// Not safe to replace stat/fstat/lstat on mingw at least, as struct stat
// has a 16-bit st_ino and we need a 64-bit one.
//
// #define stat(filename,  struct) emu_stat   (filename, struct)
// #define lstat(filename, struct) emu_stat   (filename, struct)
// #define fstat(handle,   struct) emu_fstat  (handle,   struct)
//
// But lstat doesn't exist on Windows, so we have to provide something:

#define lstat(filename, struct) stat(filename, struct)

int statfs(const char * name, struct statfs * s);

int poll(struct pollfd *ufds, unsigned long nfds, int timeout);

struct iovec {
	void *iov_base;   /* Starting address */
	size_t iov_len;   /* Number of bytes */
};

int readv (int filedes, const struct iovec *vector, size_t count);
int writev(int filedes, const struct iovec *vector, size_t count);

// The following functions are not emulations, but utilities for other 
// parts of the code where Windows API is used or windows-specific stuff 
// is needed, like codepage conversion.

bool EnableBackupRights( void );

bool ConvertEncoding (const std::string& rSource, int sourceCodePage,
	std::string& rDest, int destCodePage);
bool ConvertToUtf8   (const std::string& rSource, std::string& rDest, 
	int sourceCodePage);
bool ConvertFromUtf8 (const std::string& rSource, std::string& rDest,
	int destCodePage);
bool ConvertUtf8ToConsole(const std::string& rSource, std::string& rDest);
bool ConvertConsoleToUtf8(const std::string& rSource, std::string& rDest);

// Utility function which returns a default config file name,
// based on the path of the current executable.
std::string GetDefaultConfigFilePath(const std::string& rName);

// GetErrorMessage() returns a system error message, like strerror() 
// but for Windows error codes.
std::string GetErrorMessage(DWORD errorCode);

// console_read() is a replacement for _cgetws which requires a 
// relatively recent C runtime lib
int console_read(char* pBuffer, size_t BufferSize);

#ifdef _MSC_VER
	/* disable certain compiler warnings to be able to actually see the show-stopper ones */
	#pragma warning(disable:4101)		// unreferenced local variable
	#pragma warning(disable:4244)		// conversion, possible loss of data
	#pragma warning(disable:4267)		// conversion, possible loss of data
	#pragma warning(disable:4311)		// pointer truncation
	#pragma warning(disable:4700)		// uninitialized local variable used (hmmmmm...)
	#pragma warning(disable:4805)		// unsafe mix of type and type 'bool' in operation
	#pragma warning(disable:4800)		// forcing value to bool 'true' or 'false' (performance warning)
	#pragma warning(disable:4996)		// POSIX name for this item is deprecated
#endif // _MSC_VER

#endif // !EMU_INCLUDE && WIN32
