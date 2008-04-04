// --------------------------------------------------------------------------
//
// File
//		Name:    intercept.cpp
//		Purpose: Syscall interception code for the raidfile test
//		Created: 2003/07/22
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "intercept.h"

#ifdef HAVE_SYS_SYSCALL_H
	#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_SYS_UIO_H
	#include <sys/uio.h>
#endif

#include <errno.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

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
	// if we have __syscall, we should use it for everything
	// (on FreeBSD 7 this is required for 64-bit alignment of off_t).
	// if not, we should continue to use the old syscall().
	#ifdef HAVE___SYSCALL_NEED_DEFN
		// Need this, not declared in syscall.h nor unistd.h
		extern "C" off_t __syscall(quad_t number, ...);
	#endif
	#ifdef HAVE___SYSCALL
		#undef syscall
		#define syscall __syscall
	#endif
#endif

#include <string.h>
#include <stdio.h>

#include "MemLeakFindOn.h"

int intercept_count = 0;
const char *intercept_filename = 0;
int intercept_filedes = -1;
off_t intercept_errorafter = 0;
int intercept_errno = 0;
int intercept_syscall = 0;
off_t intercept_filepos = 0;
int intercept_delay_ms = 0;

static opendir_t*  opendir_real  = NULL;
static readdir_t*  readdir_real  = NULL;
static readdir_t*  readdir_hook  = NULL;
static closedir_t* closedir_real = NULL;
static lstat_t*    lstat_real    = NULL;
static lstat_t*    lstat_hook    = NULL;
static const char* lstat_file    = NULL;

#define SIZE_ALWAYS_ERROR	-773

void intercept_clear_setup()
{
	intercept_count = 0;
	intercept_filename = 0;
	intercept_filedes = -1;
	intercept_errorafter = 0;
	intercept_syscall = 0;
	intercept_filepos = 0;
	intercept_delay_ms = 0;
	readdir_hook = NULL;
	lstat_hook = NULL;
}

bool intercept_triggered()
{
	return intercept_count == 0;
}

void intercept_setup_error(const char *filename, unsigned int errorafter, int errortoreturn, int syscalltoerror)
{
	BOX_TRACE("Setup for error: " << filename << 
		", after " << errorafter <<
		", err " << errortoreturn <<
		", syscall " << syscalltoerror);

	intercept_count = 1;
	intercept_filename = filename;
	intercept_filedes = -1;
	intercept_errorafter = errorafter;
	intercept_syscall = syscalltoerror;
	intercept_errno = errortoreturn;
	intercept_filepos = 0;
	intercept_delay_ms = 0;
}

void intercept_setup_delay(const char *filename, unsigned int delay_after, 
	int delay_ms, int syscall_to_delay, int num_delays)
{
	BOX_TRACE("Setup for delay: " << filename <<
		", after " << delay_after <<
		", wait " << delay_ms << " ms" <<
		", times " << num_delays <<
		", syscall " << syscall_to_delay);

	intercept_count = num_delays;
	intercept_filename = filename;
	intercept_filedes = -1;
	intercept_errorafter = delay_after;
	intercept_syscall = syscall_to_delay;
	intercept_errno = 0;
	intercept_filepos = 0;
	intercept_delay_ms = delay_ms;
}

bool intercept_errornow(int d, int size, int syscallnum)
{
	ASSERT(intercept_count > 0)

	if (intercept_filedes == -1)
	{
		return false; // no error please!
	}

	if (d != intercept_filedes)
	{
		return false; // no error please!
	}

	if (syscallnum != intercept_syscall)
	{
		return false; // no error please!
	}

	bool ret = false; // no error unless one of the conditions matches

	//printf("Checking for err, %d, %d, %d\n", d, size, syscallnum);

	if (intercept_delay_ms != 0)
	{
		BOX_TRACE("Delaying " << intercept_delay_ms << " ms " <<
			" for syscall " << syscallnum << 
			" at " << intercept_filepos);

		struct timespec tm;
		tm.tv_sec = intercept_delay_ms / 1000;
		tm.tv_nsec = (intercept_delay_ms % 1000) * 1000000;
		while (nanosleep(&tm, &tm) != 0 &&
			errno == EINTR) { }
	}

	if (size == SIZE_ALWAYS_ERROR)
	{
		// Looks good for an error!
		BOX_TRACE("Returning error " << intercept_errno <<
			" for syscall " << syscallnum);
		ret = true;
	}
	else if (intercept_filepos + size < intercept_errorafter)
	{
		return false; // no error please
	}
	else if (intercept_errno != 0)
	{
		BOX_TRACE("Returning error " << intercept_errno << 
			" for syscall " << syscallnum <<
			" at " << intercept_filepos);
		ret = true;
	}

	intercept_count--;
	if (intercept_count == 0)
	{
		intercept_clear_setup();
	}

	return ret;
}

int intercept_reterr()
{
	int err = intercept_errno;
	intercept_clear_setup();
	return err;
}

#define CHECK_FOR_FAKE_ERROR_COND(D, S, CALL, FAILRES) \
	if(intercept_count > 0) \
	{ \
		if(intercept_errornow(D, S, CALL)) \
		{ \
			errno = intercept_reterr(); \
			return FAILRES; \
		} \
	}

extern "C" int
open(const char *path, int flags, mode_t mode)
{
	if(intercept_count > 0)
	{
		if(intercept_filename != NULL &&
			intercept_syscall == SYS_open &&
			strcmp(path, intercept_filename) == 0)
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

	if(intercept_filename != NULL && 
		intercept_count > 0 && 
		intercept_filedes == -1)
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
open64(const char *path, int flags, mode_t mode)
{
	// With _FILE_OFFSET_BITS set to 64 this should really use (flags |
	// O_LARGEFILE) here, but not actually necessary for the tests and not
	// worth the trouble finding O_LARGEFILE
	return open(path, flags, mode);
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
	#elif defined(_FILE_OFFSET_BITS)
		// Don't bother trying to call SYS__llseek on 32 bit since it is
		// fiddly and not needed for the tests
		off_t r = syscall(SYS_lseek, fildes, (uint32_t)offset, whence);
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

void intercept_setup_readdir_hook(const char *dirname, readdir_t hookfn)
{
	if (hookfn != NULL && dirname == NULL)
	{
		dirname = intercept_filename;
		ASSERT(dirname != NULL);
	}

	if (hookfn != NULL)
	{
		TRACE2("readdir hooked to %p for %s\n", hookfn, dirname);
	}
	else if (intercept_filename != NULL)
	{
		TRACE2("readdir unhooked from %p for %s\n", readdir_hook, 
			intercept_filename);
	}

	intercept_filename = dirname;
	readdir_hook = hookfn;
}

void intercept_setup_lstat_hook(const char *filename, lstat_t hookfn)
{
	/*
	if (hookfn != NULL)
	{
		TRACE2("lstat hooked to %p for %s\n", hookfn, filename);
	}
	else
	{
		TRACE2("lstat unhooked from %p for %s\n", lstat_hook, 
			lstat_file);
	}
	*/

	lstat_file = filename;
	lstat_hook = hookfn;
}

static void * find_function(const char *pName)
{
	dlerror();
	void *result = NULL;

	#ifdef HAVE_LARGE_FILE_SUPPORT
	{
		// search for the 64-bit version first
		std::string name64(pName);
		name64 += "64";
		result = dlsym(RTLD_NEXT, name64.c_str());
		if (dlerror() == NULL && result != NULL)
		{
			return result;
		}
	}
	#endif

	result = dlsym(RTLD_NEXT, pName);
	const char *errmsg = (const char *)dlerror();

	if (errmsg == NULL)
	{
		return result;
	}

	BOX_ERROR("Failed to find real " << pName << " function: " << errmsg);
	return NULL;
}

extern "C" 
DIR *opendir(const char *dirname)
{
	if (opendir_real == NULL)
	{
		opendir_real = (opendir_t*)find_function("opendir");
	}

	if (opendir_real == NULL)
	{
		perror("cannot find real opendir");
		return NULL;
	}

	DIR* r = opendir_real(dirname);

	if (readdir_hook != NULL && 
		intercept_filename != NULL && 
		intercept_filedes == -1 && 
		strcmp(intercept_filename, dirname) == 0)
	{
		intercept_filedes = dirfd(r);
		//printf("Found file to intercept, h = %d\n", r);
	}

	return r;
}

extern "C"
struct dirent *readdir(DIR *dir)
{
	if (readdir_hook != NULL && dirfd(dir) == intercept_filedes)
	{
		return readdir_hook(dir);
	}

	if (readdir_real == NULL)
	{
		readdir_real = (readdir_t*)find_function("readdir");
	}

	if (readdir_real == NULL)
	{
		perror("cannot find real readdir");
		return NULL;
	}

	return readdir_real(dir);
}

extern "C"
int closedir(DIR *dir)
{
	if (dirfd(dir) == intercept_filedes)
	{
		intercept_filedes = -1;
	}

	if (closedir_real == NULL)
	{
		closedir_real = (closedir_t*)find_function("closedir");
	}

	if (closedir_real == NULL)
	{
		perror("cannot find real closedir");
		errno = ENOSYS;
		return -1;
	}

	return closedir_real(dir);
}

extern "C" int 
#ifdef LINUX_WEIRD_LSTAT
__lxstat(int ver, const char *file_name, STAT_STRUCT *buf)
#else
lstat(const char *file_name, STAT_STRUCT *buf)
#endif
{
	if (lstat_real == NULL)
	{
	#ifdef LINUX_WEIRD_LSTAT
		lstat_real = (lstat_t*)find_function("__lxstat");
	#else
		lstat_real = (lstat_t*)find_function("lstat");
	#endif
	}

	if (lstat_real == NULL)
	{
		perror("cannot find real lstat");
		errno = ENOSYS;
		return -1;
	}

	if (lstat_hook == NULL || strcmp(file_name, lstat_file) != 0)
	{
	#ifdef LINUX_WEIRD_LSTAT
		return lstat_real(ver, file_name, buf);
	#else
		return lstat_real(file_name, buf);
	#endif
	}

	#ifdef LINUX_WEIRD_LSTAT
	return lstat_hook(ver, file_name, buf);
	#else
	return lstat_hook(file_name, buf);
	#endif
}

#endif // n PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
