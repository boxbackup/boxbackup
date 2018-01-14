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

#ifdef MEMLEAKFINDER_FULL_MALLOC_MONITORING
	// include stdlib now, to avoid problems with having the macros defined already
	#include <cstdlib>
#endif

// global enable flag
extern bool memleakfinder_global_enable;

class MemLeakSuppressionGuard
{
	public:
	MemLeakSuppressionGuard();
	~MemLeakSuppressionGuard();
};

extern "C"
{
	void *memleakfinder_malloc(size_t size, const char *file, int line);
	void *memleakfinder_calloc(size_t blocks, size_t size, const char *file, int line);
	void *memleakfinder_realloc(void *ptr, size_t size);
	void memleakfinder_free(void *ptr);
}

void memleakfinder_init();

int memleakfinder_numleaks();

void memleakfinder_report_usage_summary();

void memleakfinder_reportleaks();

void memleakfinder_reportleaks_appendfile(const char *filename, const char *markertext);

void memleakfinder_setup_exit_report(const std::string& filename, const char *markertext);

void memleakfinder_startsectionmonitor();

void memleakfinder_traceblocksinsection();

void memleakfinder_notaleak(void *ptr);

void *operator new  (size_t size, const char *file, int line);
void *operator new[](size_t size, const char *file, int line);
// "If the object is being created as part of a new expression, and an exception is thrown, the
// object’s memory is deallocated by calling the appropriate deallocation function. If the object
// is being created with a placement new operator, the corresponding placement delete operator is
// called—that is, the delete function that takes the same additional parameters as the placement
// new operator. If no matching placement delete is found, no deallocation takes place."
// So to avoid memory leaks, we need to implement placement delete operators too.
void operator delete  (void *ptr, const char *file, int line);
void operator delete[](void *ptr, const char *file, int line);

// Define the malloc functions now, if required. These should match the definitions
// in MemLeakFindOn.h.
#ifdef MEMLEAKFINDER_FULL_MALLOC_MONITORING
	#define malloc(X)	memleakfinder_malloc(X, __FILE__, __LINE__)
	#define calloc(X, Y)	memleakfinder_calloc(X, Y, __FILE__, __LINE__)
	#define realloc		memleakfinder_realloc
	#define free		memleakfinder_free
	#define MEMLEAKFINDER_MALLOC_MONITORING_DEFINED
#endif

#endif // MEMLEAKFINDER__H

