// --------------------------------------------------------------------------
//
// File
//		Name:    Box.h
//		Purpose: Main header file for the Box project
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

#ifndef BOX__H
#define BOX__H

#include "BoxPlatform.h"

// uncomment this line to enable full memory leak finding on all malloc-ed blocks (at least, ones used by the STL)
//#define MEMLEAKFINDER_FULL_MALLOC_MONITORING

#ifndef NDEBUG
	// not available on OpenBSD... oh well.
	//#define SHOW_BACKTRACE_ON_EXCEPTION
#endif

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	// include "Utils.h"
	#define OPTIONAL_DO_BACKTRACE DumpStackBacktrace();
#else
	#define OPTIONAL_DO_BACKTRACE
#endif

#include "CommonException.h"

#ifndef NDEBUG
	
	extern bool AssertFailuresToSyslog;
	#define ASSERT_FAILS_TO_SYSLOG_ON {AssertFailuresToSyslog = true;}
	void BoxDebugAssertFailed(char *cond, char *file, int line);
	#define ASSERT(cond) {if(!(cond)) {BoxDebugAssertFailed(#cond, __FILE__, __LINE__); THROW_EXCEPTION(CommonException, AssertFailed)}}

	// Note that syslog tracing is independent of BoxDebugTraceOn, but stdout tracing is not
	extern bool BoxDebugTraceToSyslog;
	#define TRACE_TO_SYSLOG(x) {BoxDebugTraceToSyslog = x;}
	extern bool BoxDebugTraceToStdout;
	#define TRACE_TO_STDOUT(x) {BoxDebugTraceToStdout = x;}

	extern bool BoxDebugTraceOn;
	int BoxDebug_printf(const char *format, ...);
	int BoxDebugTrace(const char *format, ...);
	#define	TRACE0(msg) {BoxDebugTrace("%s", msg);}
	#define	TRACE1(msg, a0) {BoxDebugTrace(msg, a0);}
	#define	TRACE2(msg, a0, a1) {BoxDebugTrace(msg, a0, a1);}
	#define	TRACE3(msg, a0, a1, a2) {BoxDebugTrace(msg, a0, a1, a2);}
	#define	TRACE4(msg, a0, a1, a2, a3) {BoxDebugTrace(msg, a0, a1, a2, a3);}
	#define	TRACE5(msg, a0, a1, a2, a3, a4) {BoxDebugTrace(msg, a0, a1, a2, a3, a4);}
	#define	TRACE6(msg, a0, a1, a2, a3, a4, a5) {BoxDebugTrace(msg, a0, a1, a2, a3, a4, a5);}
	#define	TRACE7(msg, a0, a1, a2, a3, a4, a5, a6) {BoxDebugTrace(msg, a0, a1, a2, a3, a4, a5, a6);}
	#define	TRACE8(msg, a0, a1, a2, a3, a4, a5, a6, a7) {BoxDebugTrace(msg, a0, a1, a2, a3, a4, a5, a6, a7);}
	
	#ifndef PLATFORM_DISABLE_MEM_LEAK_TESTING
		#define BOX_MEMORY_LEAK_TESTING
	#endif
	
	// Exception names
	#define EXCEPTION_CODENAMES_EXTENDED
	
#else
	#define ASSERT_FAILS_TO_SYSLOG_ON
	#define ASSERT(cond)

	#define TRACE_TO_SYSLOG(x) {}
	#define TRACE_TO_STDOUT(x) {}

	#define TRACE0(msg)
	#define	TRACE1(msg, a0)
	#define	TRACE2(msg, a0, a1)
	#define	TRACE3(msg, a0, a1, a2)
	#define	TRACE4(msg, a0, a1, a2, a3)
	#define	TRACE5(msg, a0, a1, a2, a3, a4)
	#define	TRACE6(msg, a0, a1, a2, a3, a4, a5)
	#define	TRACE7(msg, a0, a1, a2, a3, a4, a5, a6)
	#define	TRACE8(msg, a0, a1, a2, a3, a4, a5, a6, a7)
	
	// Box Backup builds release get extra information for exception logging
	#define EXCEPTION_CODENAMES_EXTENDED
	#define EXCEPTION_CODENAMES_EXTENDED_WITH_DESCRIPTION
	
	// But in private builds, these get disabled
	// BOX_PRIVATE_BEGIN
	#undef EXCEPTION_CODENAMES_EXTENDED
	#undef EXCEPTION_CODENAMES_EXTENDED_WITH_DESCRIPTION
	// BOX_PRIVATE_END
#endif

#ifdef BOX_MEMORY_LEAK_TESTING
	// Memory leak testing
	#include "MemLeakFinder.h"
	#define MEMLEAKFINDER_NOT_A_LEAK(x)	memleakfinder_notaleak(x);
	#define MEMLEAKFINDER_START {memleakfinder_global_enable = true;}
	#define MEMLEAKFINDER_STOP {memleakfinder_global_enable = false;}
#else
	#define DEBUG_NEW new
	#define MEMLEAKFINDER_NOT_A_LEAK(x)
	#define MEMLEAKFINDER_START
	#define MEMLEAKFINDER_STOP
#endif


#define THROW_EXCEPTION(type, subtype)														\
	{																						\
		OPTIONAL_DO_BACKTRACE																\
		TRACE1("Exception thrown: " #type "(" #subtype ") at " __FILE__ "(%d)\n", __LINE__)	\
		throw type(type::subtype);															\
	}

// extra macros for converting to network byte order

// Always define a swap64 function, as it's useful.
inline uint64_t box_swap64(uint64_t x)
{
	return ((x & 0xff) << 56 |
		(x & 0xff00LL) << 40 |
		(x & 0xff0000LL) << 24 |
		(x & 0xff000000LL) << 8 |
		(x & 0xff00000000LL) >> 8 |
		(x & 0xff0000000000LL) >> 24 |
		(x & 0xff000000000000LL) >> 40 |
		(x & 0xff00000000000000LL) >> 56);
}

// Does the platform provide a built in SWAP64 we can use?
#ifdef PLATFORM_NO_BUILT_IN_SWAP64

	#define hton64(x) box_swap64(x)
	#define ntoh64(x) box_swap64(x)

#else

	#if BYTE_ORDER == BIG_ENDIAN
	
		// Less hassle than trying to find some working things
		// on Darwin PPC
		#define hton64(x) (x)
		#define ntoh64(x) (x)
	
	#else
	
		#ifdef PLATFORM_LINUX
			// On Linux, use some internal kernal stuff to do this
			#include <asm/byteorder.h>
			#define hton64 __cpu_to_be64
			#define ntoh64 __be64_to_cpu
		#else
			#define hton64 htobe64
			#define ntoh64 betoh64
		#endif
	
		// hack to make some of these work
		// version in  /usr/include/sys/endian.h  doesn't include the 'LL' at the end of the constants
		// provoking complaints from the compiler
		#ifdef __GNUC__
		#undef __swap64gen
		#define __swap64gen(x) __extension__({                                  \
				u_int64_t __swap64gen_x = (x);                                  \
																				\
				(u_int64_t)((__swap64gen_x & 0xff) << 56 |                      \
					(__swap64gen_x & 0xff00LL) << 40 |                            \
					(__swap64gen_x & 0xff0000LL) << 24 |                          \
					(__swap64gen_x & 0xff000000LL) << 8 |                         \
					(__swap64gen_x & 0xff00000000LL) >> 8 |                       \
					(__swap64gen_x & 0xff0000000000LL) >> 24 |                    \
					(__swap64gen_x & 0xff000000000000LL) >> 40 |                  \
					(__swap64gen_x & 0xff00000000000000LL) >> 56);                \
		})
		#endif // __GNUC__
	
	#endif // n BYTE_ORDER == BIG_ENDIAN

#endif // PLATFORM_NO_BUILT_IN_SWAP64

#endif // BOX__H

