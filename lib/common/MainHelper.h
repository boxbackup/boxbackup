// --------------------------------------------------------------------------
//
// File
//		Name:    MainHelper.h
//		Purpose: Helper stuff for main() programs
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------

#ifndef MAINHELPER__H
#define MAINHELPER__H

#include <stdio.h>

#ifdef NEED_BOX_VERSION_H
#	include "BoxVersion.h"
#endif

#include "BannerText.h"
#include "BoxException.h"
#include "Logging.h"

#define MAINHELPER_START \
	/* need to init memleakfinder early because we already called MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT */ \
	MEMLEAKFINDER_INIT \
	if(argc == 2 && ::strcmp(argv[1], "--version") == 0) \
	{ \
		printf("Version: " BOX_VERSION "\n"); \
		printf("Build: " BOX_BUILD_SIGNATURE "\n"); \
		return 0; \
	} \
	MEMLEAKFINDER_START \
	try {

#define MAINHELPER_END \
	} catch(BoxException &e) { \
		BOX_FATAL(e.what() << ": " << e.GetMessage()); \
		return 1; \
	} catch(std::exception &e) { \
		BOX_FATAL(e.what()); \
		return 1; \
	} catch(...) { \
		BOX_FATAL("UNKNOWN"); \
		return 1; \
	}

#ifdef BOX_MEMORY_LEAK_TESTING
	#define MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT(file, marker)				\
		memleakfinder_setup_exit_report(file, marker);
#else
	#define MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT(file, marker)
#endif // BOX_MEMORY_LEAK_TESTING


#endif // MAINHELPER__H

