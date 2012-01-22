// --------------------------------------------------------------------------
//
// File
//		Name:    FileModificationTime.cpp
//		Purpose: Function for getting file modification time.
//		Created: 2010/02/15
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/stat.h>

#include "BoxTime.h"
#include "FileModificationTime.h"

#include "MemLeakFindOn.h"

box_time_t FileModificationTime(const EMU_STRUCT_STAT &st)
{
#ifndef HAVE_STRUCT_STAT_ST_MTIMESPEC
	box_time_t datamodified = ((int64_t)st.st_mtime) * (MICRO_SEC_IN_SEC_LL);
#else
	box_time_t datamodified = (((int64_t)st.st_mtimespec.tv_nsec) / NANO_SEC_IN_USEC_LL)
			+ (((int64_t)st.st_mtimespec.tv_sec) * (MICRO_SEC_IN_SEC_LL));
#endif
	
	return datamodified;
}

box_time_t FileAttrModificationTime(const EMU_STRUCT_STAT &st)
{
	box_time_t statusmodified =
#ifdef HAVE_STRUCT_STAT_ST_MTIMESPEC
		(((int64_t)st.st_ctimespec.tv_nsec) / (NANO_SEC_IN_USEC_LL)) +
		(((int64_t)st.st_ctimespec.tv_sec)  * (MICRO_SEC_IN_SEC_LL));
#elif defined HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
		(((int64_t)st.st_ctim.tv_nsec) / (NANO_SEC_IN_USEC_LL)) +
		(((int64_t)st.st_ctim.tv_sec)  * (MICRO_SEC_IN_SEC_LL));
#elif defined HAVE_STRUCT_STAT_ST_ATIMENSEC
		(((int64_t)st.st_ctimensec) / (NANO_SEC_IN_USEC_LL)) +
		(((int64_t)st.st_ctime)     * (MICRO_SEC_IN_SEC_LL));
#else // no nanoseconds anywhere
		(((int64_t)st.st_ctime) * (MICRO_SEC_IN_SEC_LL));
#endif
	
	return statusmodified;
}

box_time_t FileModificationTimeMaxModAndAttr(const EMU_STRUCT_STAT &st)
{
#ifndef HAVE_STRUCT_STAT_ST_MTIMESPEC
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

