/*************************************************************************************************
 * System configurations for QDBM
 *                                                      Copyright (C) 2000-2007 Mikio Hirabayashi
 * This file is part of QDBM, Quick Database Manager.
 * QDBM is free software; you can redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software Foundation; either version
 * 2.1 of the License or any later version.  QDBM is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License along with QDBM; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA.
 *************************************************************************************************/


#ifndef _MYCONF_H                        /* duplication check */
#define _MYCONF_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif



/*************************************************************************************************
 * system discrimination
 *************************************************************************************************/


#if defined(__linux__)

#define _SYS_LINUX_
#define _QDBM_SYSNAME  "Linux"

#elif defined(__FreeBSD__)

#define _SYS_FREEBSD_
#define _QDBM_SYSNAME  "FreeBSD"

#elif defined(__NetBSD__)

#define _SYS_NETBSD_
#define _QDBM_SYSNAME  "NetBSD"

#elif defined(__OpenBSD__)

#define _SYS_OPENBSD_
#define _QDBM_SYSNAME  "OpenBSD"

#elif defined(__sun__)

#define _SYS_SUNOS_
#define _QDBM_SYSNAME  "SunOS"

#elif defined(__hpux)

#define _SYS_HPUX_
#define _QDBM_SYSNAME  "HP-UX"

#elif defined(__osf)

#define _SYS_TRU64_
#define _QDBM_SYSNAME  "Tru64"

#elif defined(_AIX)

#define _SYS_AIX_
#define _QDBM_SYSNAME  "AIX"

#elif defined(__APPLE__) && defined(__MACH__)

#define _SYS_MACOSX_
#define _QDBM_SYSNAME  "Mac OS X"

#elif defined(_MSC_VER)

#define _SYS_MSVC_
#define _QDBM_SYSNAME  "Windows (VC++)"

#elif defined(_WIN32)

#define _SYS_MINGW_
#define _QDBM_SYSNAME  "Windows (MinGW)"

#elif defined(__CYGWIN__)

#define _SYS_CYGWIN_
#define _QDBM_SYSNAME  "Windows (Cygwin)"

#elif defined(__riscos__) || defined(__riscos)

#define _SYS_RISCOS_
#define _QDBM_SYSNAME  "RISC OS"

#else

#define _SYS_GENERIC_
#define _QDBM_SYSNAME  "Generic"

#endif



/*************************************************************************************************
 * general headers
 *************************************************************************************************/


#if defined(_SYS_MSVC_)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <direct.h>
#include <windows.h>
#include <io.h>

#elif defined(_SYS_MINGW_)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <windows.h>
#include <io.h>

#elif defined(_SYS_CYGWIN_)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <fcntl.h>
#include <dirent.h>
#include <windows.h>
#include <io.h>

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <fcntl.h>
#include <dirent.h>

#endif



/*************************************************************************************************
 * notation of filesystems
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)

#define MYPATHCHR       '\\'
#define MYPATHSTR       "\\"
#define MYEXTCHR        '.'
#define MYEXTSTR        "."
#define MYCDIRSTR       "."
#define MYPDIRSTR       ".."

#elif defined(_SYS_RISCOS_)

#define MYPATHCHR       '.'
#define MYPATHSTR       "."
#define MYEXTCHR        '/'
#define MYEXTSTR        "/"
#define MYCDIRSTR       "@"
#define MYPDIRSTR       "^"

#else

#define MYPATHCHR       '/'
#define MYPATHSTR       "/"
#define MYEXTCHR        '.'
#define MYEXTSTR        "."
#define MYCDIRSTR       "."
#define MYPDIRSTR       ".."

#endif



/*************************************************************************************************
 * for dosish filesystems
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_) || defined(_SYS_CYGWIN_)

#undef UNICODE
#undef open

#define \
  open(pathname, flags, mode) \
  open(pathname, flags | O_BINARY, mode)

#define \
  lstat(pathname, buf) \
  _qdbm_win32_lstat(pathname, buf)

int _qdbm_win32_lstat(const char *pathname, struct stat *buf);

#else

#undef O_BINARY
#undef O_TEXT
#undef setmode

#define O_BINARY           0
#define O_TEXT             1

#define \
  setmode(fd, mode) \
  (O_BINARY)

#endif



/*************************************************************************************************
 * for POSIX thread
 *************************************************************************************************/


#if defined(MYPTHREAD)

#define _qdbm_ptsafe       TRUE

void *_qdbm_settsd(void *ptr, int size, const void *initval);

#else

#define _qdbm_ptsafe       FALSE

#define \
  _qdbm_settsd(ptr, size, initval) \
  (NULL)

#endif



/*************************************************************************************************
 * for systems without file locking
 *************************************************************************************************/


#if defined(_SYS_RISCOS_) || defined(MYNOLOCK)

#undef fcntl

#define \
  fcntl(fd, cmd, lock) \
  (0)

#endif



/*************************************************************************************************
 * for systems without mmap
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_) || \
  defined(_SYS_FREEBSD_) || defined(_SYS_NETBSD_) || defined(_SYS_OPENBSD_) || \
  defined(_SYS_AIX_) || defined(_SYS_RISCOS_) || defined(MYNOMMAP)

#undef PROT_EXEC
#undef PROT_READ
#undef PROT_WRITE
#undef PROT_NONE
#undef MAP_FIXED
#undef MAP_SHARED
#undef MAP_PRIVATE
#undef MAP_FAILED
#undef MS_ASYNC
#undef MS_SYNC
#undef MS_INVALIDATE
#undef mmap
#undef munmap
#undef msync
#undef mflush

#define PROT_EXEC      (1 << 0)
#define PROT_READ      (1 << 1)
#define PROT_WRITE     (1 << 2)
#define PROT_NONE      (1 << 3)
#define MAP_FIXED      1
#define MAP_SHARED     2
#define MAP_PRIVATE    3
#define MAP_FAILED     ((void *)-1)
#define MS_ASYNC       (1 << 0)
#define MS_SYNC        (1 << 1)
#define MS_INVALIDATE  (1 << 2)

#define \
  mmap(start, length, prot, flags, fd, offset) \
  _qdbm_mmap(start, length, prot, flags, fd, offset)

#define \
  munmap(start, length) \
  _qdbm_munmap(start, length)

#define \
  msync(start, length, flags) \
  _qdbm_msync(start, length, flags)

#define \
  mflush(start, length, flags) \
  _qdbm_msync(start, length, flags)

void *_qdbm_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int _qdbm_munmap(void *start, size_t length);
int _qdbm_msync(const void *start, size_t length, int flags);

#else

#undef mflush
#define \
  mflush(start, length, flags) \
  (0)

#endif



/*************************************************************************************************
 * for reentrant time routines
 *************************************************************************************************/


struct tm *_qdbm_gmtime(const time_t *timep, struct tm *result);
struct tm *_qdbm_localtime(const time_t *timep, struct tm *result);



/*************************************************************************************************
 * for systems without times
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)

#undef times
#undef sysconf

struct tms {
  clock_t tms_utime;
  clock_t tms_stime;
  clock_t tms_cutime;
  clock_t tms_cstime;
};

#define \
  times(buf) \
  _qdbm_times(buf)

#define \
  sysconf(name) \
  (CLOCKS_PER_SEC)

clock_t _qdbm_times(struct tms *buf);

#endif



/*************************************************************************************************
 * for Win32
 *************************************************************************************************/


#if defined(_SYS_MSVC_) || defined(_SYS_MINGW_)

#undef F_WRLCK
#undef F_RDLCK
#undef F_SETLK
#undef F_SETLKW
#undef fcntl
#undef ftruncate
#undef fsync
#undef mkdir
#undef rename

#define F_WRLCK        0
#define F_RDLCK        1
#define F_SETLK        0
#define F_SETLKW       1

struct flock {
  int l_type;
  int l_whence;
  int l_start;
  int l_len;
  int l_pid;
};

#define \
  fcntl(fd, cmd, lock) \
  _qdbm_win32_fcntl(fd, cmd, lock)

#define \
  ftruncate(fd, length) \
  _chsize(fd, length)

#define \
  fsync(fd) \
  (0)

#define \
  mkdir(pathname, mode) \
  mkdir(pathname)

#define \
  rename(oldpath, newpath) \
  (unlink(newpath), rename(oldpath, newpath))

int _qdbm_win32_fcntl(int fd, int cmd, struct flock *lock);

#endif


#if defined(_SYS_MSVC_)

#undef S_ISDIR
#undef S_ISREG
#undef opendir
#undef closedir
#undef readdir

#define S_ISDIR(x)     (x & _S_IFDIR)
#define S_ISREG(x)     (x & _S_IFREG)

struct dirent {
  char d_name[1024];
};

typedef struct {
  HANDLE fh;
  WIN32_FIND_DATA data;
  struct dirent de;
  int first;
} DIR;

#define \
  opendir(name) \
  _qdbm_win32_opendir(name)

#define \
  closedir(dir) \
  _qdbm_win32_closedir(dir)

#define \
  readdir(dir) \
  _qdbm_win32_readdir(dir)

DIR *_qdbm_win32_opendir(const char *name);

int _qdbm_win32_closedir(DIR *dir);

struct dirent *_qdbm_win32_readdir(DIR *dir);

#endif



/*************************************************************************************************
 * for checking information of the system
 *************************************************************************************************/


int _qdbm_vmemavail(size_t size);



/*************************************************************************************************
 * for ZLIB
 *************************************************************************************************/


enum {
  _QDBM_ZMZLIB,
  _QDBM_ZMRAW,
  _QDBM_ZMGZIP
};


extern char *(*_qdbm_deflate)(const char *, int, int *, int);

extern char *(*_qdbm_inflate)(const char *, int, int *, int);

extern unsigned int (*_qdbm_getcrc)(const char *, int);



/*************************************************************************************************
 * for LZO
 *************************************************************************************************/


extern char *(*_qdbm_lzoencode)(const char *, int, int *);

extern char *(*_qdbm_lzodecode)(const char *, int, int *);



/*************************************************************************************************
 * for BZIP2
 *************************************************************************************************/


extern char *(*_qdbm_bzencode)(const char *, int, int *);

extern char *(*_qdbm_bzdecode)(const char *, int, int *);



/*************************************************************************************************
 * for ICONV
 *************************************************************************************************/


extern char *(*_qdbm_iconv)(const char *, int, const char *, const char *, int *, int *);

extern const char *(*_qdbm_encname)(const char *, int);



/*************************************************************************************************
 * common settings
 *************************************************************************************************/


#undef TRUE
#define TRUE           1
#undef FALSE
#define FALSE          0

#define sizeof(a)      ((int)sizeof(a))

int _qdbm_dummyfunc(void);



#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
