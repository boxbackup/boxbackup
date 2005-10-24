// --------------------------------------------------------------------------
//
// File
//		Name:    MemLeakFinder.h
//		Purpose: Memory leak finder
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#ifndef MEMLEAKFINDER__H
#define MEMLEAKFINDER__H

#define DEBUG_NEW new(__FILE__,__LINE__)

#ifdef MEMLEAKFINDER_FULL_MALLOC_MONITORING
	// include stdlib now, to avoid problems with having the macros defined already
	#include <stdlib.h>
#endif

// global enable flag
extern bool memleakfinder_global_enable;

extern "C"
{
	void *memleakfinder_malloc(size_t size, const char *file, int line);
	void *memleakfinder_realloc(void *ptr, size_t size);
	void memleakfinder_free(void *ptr);
}

int memleakfinder_numleaks();

void memleakfinder_reportleaks();

void memleakfinder_reportleaks_appendfile(const char *filename, const char *markertext);

void memleakfinder_setup_exit_report(const char *filename, const char *markertext);

void memleakfinder_startsectionmonitor();

void memleakfinder_traceblocksinsection();

void memleakfinder_notaleak(void *ptr);

void *operator new(size_t size, const char *file, int line);
void *operator new[](size_t size, const char *file, int line);

void operator delete(void *ptr) throw ();
void operator delete[](void *ptr) throw ();

// define the malloc functions now, if required
#ifdef MEMLEAKFINDER_FULL_MALLOC_MONITORING
	#define malloc(X)	memleakfinder_malloc(X, __FILE__, __LINE__)
	#define realloc		memleakfinder_realloc
	#define free		memleakfinder_free
	#define MEMLEAKFINDER_MALLOC_MONITORING_DEFINED
#endif


#endif // MEMLEAKFINDER__H

