// --------------------------------------------------------------------------
//
// File
//		Name:    intercept.cpp
//		Purpose: Syscall interception code for the raidfile test
//		Created: 2003/07/22
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_SYS_SYSCALL_H
	#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>

#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

#if !defined(HAVE_SYSCALL) && !defined(HAVE___SYSCALL) && !defined(HAVE___SYSCALL_NEED_DEFN)
	#define PLATFORM_NO_SYSCALL
#endif

#ifdef PLATFORM_NO_SYSCALL
	// For some reason, syscall just doesn't work on Darwin
	// so instead, we build functions using assembler in a varient
	// of the technique used in the Darwin Libc
	extern "C" int
	TEST_open(const char *path, int flags, mode_t mode);
	extern "C" int
	TEST_close(int d);
	extern "C" ssize_t
	TEST_write(int d, const void *buf, size_t nbytes);
	extern "C" ssize_t
	TEST_read(int d, void *buf, size_t nbytes);
	extern "C" ssize_t
	TEST_readv(int d, const struct iovec *iov, int iovcnt);
	extern "C" off_t
	TEST_lseek(int fildes, off_t offset, int whence);
#else
	#ifdef HAVE___SYSCALL_NEED_DEFN
		// Need this, not declared in syscall.h nor unistd.h
		extern "C" off_t __syscall(quad_t number, ...);
	#endif
	#ifndef HAVE_SYSCALL
		#undef syscall
		#define syscall __syscall
	#endif
#endif

#include <string.h>
#include <stdio.h>

#include "MemLeakFindOn.h"

bool intercept_enabled = false;
const char *intercept_filename = 0;
int intercept_filedes = -1;
off_t intercept_errorafter = 0;
int intercept_errno = 0;
int intercept_syscall = 0;
off_t intercept_filepos = 0;

#define SIZE_ALWAYS_ERROR	-773

void intercept_clear_setup()
{
	intercept_enabled = false;
	intercept_filename = 0;
	intercept_filedes = -1;
	intercept_errorafter = 0;
	intercept_syscall = 0;
	intercept_filepos = 0;
}

bool intercept_triggered()
{
	return !intercept_enabled;
}

void intercept_setup_error(const char *filename, unsigned int errorafter, int errortoreturn, int syscalltoerror)
{
	TRACE4("Setup for error: %s, after %d, err %d, syscall %d\n", filename, errorafter, errortoreturn, syscalltoerror);
	intercept_enabled = true;
	intercept_filename = filename;
	intercept_filedes = -1;
	intercept_errorafter = errorafter;
	intercept_syscall = syscalltoerror;
	intercept_errno = errortoreturn;
	intercept_filepos = 0;
}

bool intercept_errornow(int d, int size, int syscallnum)
{
	if(intercept_filedes != -1 && d == intercept_filedes && syscallnum == intercept_syscall)
	{
		//printf("Checking for err, %d, %d, %d\n", d, size, syscallnum);
		if(size == SIZE_ALWAYS_ERROR)
		{
			// Looks good for an error!
			TRACE2("Returning error %d for syscall %d\n", intercept_errno, syscallnum);
			return true;
		}
		// where are we in the file?
		if(intercept_filepos >= intercept_errorafter || intercept_filepos >= ((off_t)intercept_errorafter - size))
		{
			TRACE3("Returning error %d for syscall %d, file pos %d\n", intercept_errno, syscallnum, (int)intercept_filepos);
			return true;
		}
	}
	return false;	// no error please!
}

int intercept_reterr()
{
	intercept_enabled = false;
	intercept_filename = 0;
	intercept_filedes = -1;
	intercept_errorafter = 0;
	intercept_syscall = 0;
	return intercept_errno;
}

#define CHECK_FOR_FAKE_ERROR_COND(D, S, CALL, FAILRES)	\
	if(intercept_enabled)					\
	{										\
		if(intercept_errornow(D, S, CALL))	\
		{									\
			errno = intercept_reterr();		\
			return FAILRES;					\
		}									\
	}

extern "C" int
open(const char *path, int flags, mode_t mode)
{
	if(intercept_enabled)
	{
		if(intercept_syscall == SYS_open && strcmp(path, intercept_filename) == 0)
		{
			errno = intercept_reterr();
			return -1;
		}
	}
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_open(path, flags, mode);
#else
	int r = syscall(SYS_open, path, flags, mode);
#endif
	if(intercept_enabled && intercept_filedes == -1)
	{
		// Right file?
		if(strcmp(intercept_filename, path) == 0)
		{
			intercept_filedes = r;
			//printf("Found file to intercept, h = %d\n", r);
		}
	}
	return r;
}

extern "C" int
close(int d)
{
	CHECK_FOR_FAKE_ERROR_COND(d, SIZE_ALWAYS_ERROR, SYS_close, -1);
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_close(d);
#else
	int r = syscall(SYS_close, d);
#endif
	if(r == 0)
	{
		if(d == intercept_filedes)
		{
			intercept_filedes = -1;
		}
	}
	return r;
}

extern "C" ssize_t
write(int d, const void *buf, size_t nbytes)
{
	CHECK_FOR_FAKE_ERROR_COND(d, nbytes, SYS_write, -1);
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_write(d, buf, nbytes);
#else
	int r = syscall(SYS_write, d, buf, nbytes);
#endif
	if(r != -1)
	{
		intercept_filepos += r;
	}
	return r;
}

extern "C" ssize_t
read(int d, void *buf, size_t nbytes)
{
	CHECK_FOR_FAKE_ERROR_COND(d, nbytes, SYS_read, -1);
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_read(d, buf, nbytes);
#else
	int r = syscall(SYS_read, d, buf, nbytes);
#endif
	if(r != -1)
	{
		intercept_filepos += r;
	}
	return r;
}

extern "C" ssize_t
readv(int d, const struct iovec *iov, int iovcnt)
{
	// how many bytes?
	int nbytes = 0;
	for(int b = 0; b < iovcnt; ++b)
	{
		nbytes += iov[b].iov_len;
	}

	CHECK_FOR_FAKE_ERROR_COND(d, nbytes, SYS_readv, -1);
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_readv(d, iov, iovcnt);
#else
	int r = syscall(SYS_readv, d, iov, iovcnt);
#endif
	if(r != -1)
	{
		intercept_filepos += r;
	}
	return r;
}

extern "C" off_t
lseek(int fildes, off_t offset, int whence)
{
	// random magic for lseek syscall, see /usr/src/lib/libc/sys/lseek.c
	CHECK_FOR_FAKE_ERROR_COND(fildes, 0, SYS_lseek, -1);
#ifdef PLATFORM_NO_SYSCALL
	int r = TEST_lseek(fildes, offset, whence);
#else
	#ifdef HAVE_LSEEK_DUMMY_PARAM
		off_t r = syscall(SYS_lseek, fildes, 0 /* extra 0 required here! */, offset, whence);
	#else
		off_t r = syscall(SYS_lseek, fildes, offset, whence);
	#endif
#endif
	if(r != -1)
	{
		intercept_filepos = r;
	}
	return r;
}

#endif // n PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
