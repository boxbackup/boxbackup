// --------------------------------------------------------------------------
//
// File
//		Name:    intercept.h
//		Purpose: Syscall interception code for unit tests
//		Created: 2006/11/29
//
// --------------------------------------------------------------------------

#ifndef INTERCEPT_H
#define INTERCEPT_H
#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

#include <dirent.h>

#ifdef __NetBSD__  //__NetBSD_Version__ is defined in sys/param.h
#include <sys/param.h>
#endif

#if defined __NetBSD_Version__ && __NetBSD_Version__ >= 399000800 //3.99.8 vers.
#define FUNC_OPENDIR "__opendir30"
#define FUNC_READDIR "__readdir30"
#else
#define FUNC_OPENDIR "opendir"
#define FUNC_READDIR "readdir"
#endif

#include <sys/types.h>
#include <sys/stat.h>

extern "C"
{
	typedef DIR           *(opendir_t) (const char *name);
	typedef struct dirent *(readdir_t) (DIR *dir);
	typedef struct dirent *(readdir_t) (DIR *dir);
	typedef int            (closedir_t)(DIR *dir);
#if defined __GNUC__ && __GNUC__ >= 2
	#define LINUX_WEIRD_LSTAT
	#define STAT_STRUCT struct stat /* should be stat64 */
		typedef int    (lstat_t)   (int ver, const char *file_name, 
					    STAT_STRUCT *buf);
#else
	#define STAT_STRUCT struct stat
		typedef int    (lstat_t)   (const char *file_name, 
					    STAT_STRUCT *buf);
#endif
}

typedef int (lstat_post_hook_t) (int old_ret, const char *file_name,
	struct stat *buf);

void intercept_setup_error(const char *filename, unsigned int errorafter, 
	int errortoreturn, int syscalltoerror);
void intercept_setup_delay(const char *filename, unsigned int delay_after,
	int delay_ms, int syscall_to_delay, int num_delays);
bool intercept_triggered();

void intercept_setup_readdir_hook(const char *dirname,  readdir_t hookfn);
void intercept_setup_lstat_hook  (const char *filename, lstat_t   hookfn);
void intercept_setup_lstat_post_hook(lstat_post_hook_t hookfn);
void intercept_setup_stat_post_hook (lstat_post_hook_t hookfn);

void intercept_clear_setup();

// Some newer architectures don't have an open() syscall, but use openat() instead.
// In these cases we define SYS_open (which is otherwise undefined) to equal SYS_openat
// (which is defined) so that everywhere else we can call intercept_setup_error(SYS_open)
// without caring about the difference.
// https://chromium.googlesource.com/linux-syscall-support/
#if !HAVE_DECL_SYS_OPEN && HAVE_DECL_SYS_OPENAT
#	define SYS_open SYS_openat
#endif

#endif // !PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
#endif // !INTERCEPT_H
