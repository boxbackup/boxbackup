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

#include "BoxException.h"

#define MAINHELPER_START									\
	if(argc == 2 && ::strcmp(argv[1], "--version") == 0)	\
	{ printf(BOX_VERSION "\n"); return 0; }					\
	MEMLEAKFINDER_START										\
	try {
#define MAINHELPER_END																\
	} catch(BoxException &e) {														\
	printf("Exception: %s (%d/%d)\n", e.what(), e.GetType(), e.GetSubType());		\
	return 1;																		\
	} catch(std::exception &e) {													\
	printf("Exception: %s\n", e.what());											\
	return 1;																		\
	} catch(...) {																	\
	printf("Exception: <UNKNOWN>\n");												\
	return 1; }

#ifdef BOX_MEMORY_LEAK_TESTING
	#define MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT(file, marker)				\
		memleakfinder_setup_exit_report(file, marker);
#else
	#define MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT(file, marker)
#endif // BOX_MEMORY_LEAK_TESTING


#endif // MAINHELPER__H

