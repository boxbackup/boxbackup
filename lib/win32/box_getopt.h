#if defined _MSC_VER || defined __MINGW32__
#define	REPLACE_GETOPT 1 /* use this getopt as the system getopt(3) */
#else
#define	REPLACE_GETOPT 0 // force a conflict if included multiple times
#endif

#if REPLACE_GETOPT
#	include "bsd_getopt.h"
#	define BOX_BSD_GETOPT
#else
#	include <getopt.h>
#	undef BOX_BSD_GETOPT
#endif

