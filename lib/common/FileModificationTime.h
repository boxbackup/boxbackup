// --------------------------------------------------------------------------
//
// File
//		Name:    FileModificationTime.h
//		Purpose: Function for getting file modification time.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef FILEMODIFICATIONTIME__H
#define FILEMODIFICATIONTIME__H

#include <sys/stat.h>

#include "BoxTime.h"

inline box_time_t FileModificationTime(struct stat &st)
{
#ifdef PLATFORM_stat_SHORT_mtime
	box_time_t datamodified = ((int64_t)st.st_mtime) * (MICRO_SEC_IN_SEC_LL);
#else
	box_time_t datamodified = (((int64_t)st.st_mtimespec.tv_nsec) / NANO_SEC_IN_USEC_LL)
			+ (((int64_t)st.st_mtimespec.tv_sec) * (MICRO_SEC_IN_SEC_LL));
#endif
	
	return datamodified;
}

inline box_time_t FileAttrModificationTime(struct stat &st)
{
#ifdef PLATFORM_stat_SHORT_mtime
	box_time_t statusmodified = ((int64_t)st.st_ctime) * (MICRO_SEC_IN_SEC_LL);
#else
	box_time_t statusmodified = (((int64_t)st.st_ctimespec.tv_nsec) / NANO_SEC_IN_USEC_LL)
			+ (((int64_t)st.st_ctimespec.tv_sec) * (MICRO_SEC_IN_SEC_LL));
#endif
	
	return statusmodified;
}

inline box_time_t FileModificationTimeMaxModAndAttr(struct stat &st)
{
#ifdef PLATFORM_stat_SHORT_mtime
	box_time_t datamodified = ((int64_t)st.st_mtime) * (MICRO_SEC_IN_SEC_LL);
	box_time_t statusmodified = ((int64_t)st.st_ctime) * (MICRO_SEC_IN_SEC_LL);
#else
	box_time_t datamodified = (((int64_t)st.st_mtimespec.tv_nsec) / NANO_SEC_IN_USEC_LL)
			+ (((int64_t)st.st_mtimespec.tv_sec) * (MICRO_SEC_IN_SEC_LL));
	box_time_t statusmodified = (((int64_t)st.st_ctimespec.tv_nsec) / NANO_SEC_IN_USEC_LL)
			+ (((int64_t)st.st_ctimespec.tv_sec) * (MICRO_SEC_IN_SEC_LL));
#endif
	
	return (datamodified > statusmodified)?datamodified:statusmodified;
}

#endif // FILEMODIFICATIONTIME__H

