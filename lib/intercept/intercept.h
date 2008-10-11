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

#include <sys/types.h>
#include <sys/stat.h>

extern "C"
{
	typedef DIR           *(opendir_t) (const char *name);
	typedef struct dirent *(readdir_t) (DIR *dir);
	typedef struct dirent *(readdir_t) (DIR *dir);
	typedef int            (closedir_t)(DIR *dir);
#if _FILE_OFFSET_BITS == 64
	#define DEFINE_ONLY_OPEN64
#endif
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

#endif // !PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
#endif // !INTERCEPT_H
