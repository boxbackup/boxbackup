// --------------------------------------------------------------------------
//
// File
//		Name:    MemLeakFinder.cpp
//		Purpose: Memory leak finder
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef BOX_MEMORY_LEAK_TESTING

#undef malloc
#undef realloc
#undef free

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_EXECINFO_H
#	include <execinfo.h>
#endif

#ifdef HAVE_PROCESS_H
#	include <process.h>
#endif

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <cstdlib> // for std::atexit
#include <map>
#include <set>
#include <string>

#include "Exception.h"
#include "MemLeakFinder.h"
#include "Utils.h"

static bool memleakfinder_initialised = false;
bool memleakfinder_global_enable = false;

#if !defined BOX_RELEASE_BUILD && defined HAVE_EXECINFO_H
#	define BOX_MEMORY_LEAK_BACKTRACE_ENABLED
#endif

typedef struct
{
	size_t size;
	const char *file;
	int line;
#ifdef BOX_MEMORY_LEAK_BACKTRACE_ENABLED
	void  *stack_frames[20];
	size_t stack_size;
#endif
} MallocBlockInfo;

typedef struct
{
	size_t size;
	const char *file;
	int line;
	bool array;
#ifdef BOX_MEMORY_LEAK_BACKTRACE_ENABLED
	void  *stack_frames[20];
	size_t stack_size;
#endif
} ObjectInfo;

namespace
{
	static std::map<void *, MallocBlockInfo> sMallocBlocks;
	static std::map<void *, ObjectInfo> sObjectBlocks;
	static bool sTrackingDataDestroyed = false;

	static class DestructionWatchdog
	{
		public:
		~DestructionWatchdog()
		{
			sTrackingDataDestroyed = true;
		}
	}
	sWatchdog;
	
	static bool sTrackMallocInSection = false;
	static std::set<void *> sSectionMallocBlocks;
	static bool sTrackObjectsInSection = false;
	static std::map<void *, ObjectInfo> sSectionObjectBlocks;
	
	static std::set<void *> sNotLeaks;

	void *sNotLeaksPre[1024];
	size_t sNotLeaksPreNum = 0;
}

void memleakfinder_report_on_signal(int unused)
{
	// this is not safe! do not send SIGUSR1 to a process
	// in a production environment!
	memleakfinder_report_usage_summary();
}

void memleakfinder_init()
{
	ASSERT(!memleakfinder_initialised);

	{
		// allocates a permanent buffer on Solaris.
		// not a leak?
		std::ostringstream oss;
	}

	memleakfinder_initialised = true;

	#if defined WIN32
		// no signals, no way to trigger event yet
	#else
		struct sigaction newact, oldact;
		newact.sa_handler = memleakfinder_report_on_signal;
		newact.sa_flags = SA_RESTART;
		sigemptyset(&newact.sa_mask);
		if (::sigaction(SIGUSR1, &newact, &oldact) != 0)
		{
			BOX_ERROR("Failed to install USR1 signal handler");
			THROW_EXCEPTION(CommonException, Internal);
		}
		ASSERT(oldact.sa_handler == 0);
	#endif // WIN32
}

MemLeakSuppressionGuard::MemLeakSuppressionGuard()
{
	ASSERT(memleakfinder_global_enable);
	memleakfinder_global_enable = false;
}

MemLeakSuppressionGuard::~MemLeakSuppressionGuard()
{
	ASSERT_NOTHROW(!memleakfinder_global_enable);
	memleakfinder_global_enable = true;
}

// these functions may well allocate memory, which we don't want to track.
static int sInternalAllocDepth = 0;

class InternalAllocGuard
{
	public:
	InternalAllocGuard () { sInternalAllocDepth++; }
	~InternalAllocGuard() { sInternalAllocDepth--; }
};

void memleakfinder_malloc_add_block(void *b, size_t size, const char *file, int line)
{
	InternalAllocGuard guard;

	if(b != 0)
	{
		MallocBlockInfo i;
		i.size = size;
		i.file = file;
		i.line = line;
		sMallocBlocks[b] = i;
		
		if(sTrackMallocInSection)
		{
			sSectionMallocBlocks.insert(b);
		}
	}
}

void *memleakfinder_malloc(size_t size, const char *file, int line)
{
	InternalAllocGuard guard;

	void *b = std::malloc(size);

	if(!memleakfinder_global_enable)
	{
		// We may not be tracking this allocation, but if
		// someone realloc()s the buffer later then it will
		// trigger an untracked buffer warning, which we don't
		// want to see either.
		memleakfinder_notaleak(b);
		return b;
	}

	if(!memleakfinder_initialised)   return b;

	memleakfinder_malloc_add_block(b, size, file, line);

	//TRACE4("malloc(), %d, %s, %d, %08x\n", size, file, line, b);
	return b;
}

void *memleakfinder_calloc(size_t blocks, size_t size, const char *file, int line)
{
	void *block = memleakfinder_malloc(blocks * size, file, line);
	if (block != 0)
	{
		memset(block, 0, blocks * size);
	}
	return block;
}

void *memleakfinder_realloc(void *ptr, size_t size)
{
	if(!ptr)
	{
		return memleakfinder_malloc(size, "realloc", 0);
	}

	if(!size)
	{
		memleakfinder_free(ptr);
		return NULL;
	}

	InternalAllocGuard guard;

	ASSERT(ptr != NULL);
	if(!ptr) return NULL; // defensive

	if(!memleakfinder_global_enable || !memleakfinder_initialised)
	{
		ptr = std::realloc(ptr, size);
		if(!memleakfinder_global_enable)
		{
			// We may not be tracking this allocation, but if
			// someone realloc()s the buffer later then it will
			// trigger an untracked buffer warning, which we don't
			// want to see either.
			memleakfinder_notaleak(ptr);
		}
		return ptr;
	}

	// Check it's been allocated
	std::map<void *, MallocBlockInfo>::iterator i(sMallocBlocks.find(ptr));
	std::set<void *>::iterator j(sNotLeaks.find(ptr));

	if(i == sMallocBlocks.end() && j == sNotLeaks.end())
	{
		BOX_WARNING("Block " << ptr << " realloc()ated, but not "
			"in list. Error? Or allocated in startup static "
			"objects?");
	}

	if(j != sNotLeaks.end())
	{
		// It's in the list of not-leaks, so don't warn about it,
		// but it's being reallocated, so remove it from the list too,
		// in case it's reassigned, and add the new block below.
		sNotLeaks.erase(j);
	}

	void *b = std::realloc(ptr, size);

	if(i != sMallocBlocks.end())
	{
		// Worked?
		if(b != 0)
		{
			// Update map
			MallocBlockInfo inf = i->second;
			inf.size = size;
			sMallocBlocks.erase(i);
			sMallocBlocks[b] = inf;

			if(sTrackMallocInSection)
			{
				std::set<void *>::iterator si(sSectionMallocBlocks.find(ptr));
				if(si != sSectionMallocBlocks.end()) sSectionMallocBlocks.erase(si);
				sSectionMallocBlocks.insert(b);
			}
		}
	}
	else
	{
		memleakfinder_malloc_add_block(b, size, "FOUND-IN-REALLOC", 0);
	}

	//TRACE3("realloc(), %d, %08x->%08x\n", size, ptr, b);
	return b;
}

void memleakfinder_free(void *ptr)
{
	InternalAllocGuard guard;

	if(memleakfinder_global_enable && memleakfinder_initialised)
	{
		// Check it's been allocated
		std::map<void *, MallocBlockInfo>::iterator i(sMallocBlocks.find(ptr));
		std::set<void *>::iterator j(sNotLeaks.find(ptr));

		if(i != sMallocBlocks.end())
		{
			sMallocBlocks.erase(i);
		}
		else if(j != sNotLeaks.end())
		{
			// It's in the list of not-leaks, so don't warn
			// about it, but it's being freed, so remove it
			// from the list too, in case it's reassigned.
			sNotLeaks.erase(j);
		}
		else
		{
			BOX_WARNING("Block " << ptr << " freed, but not known. Error? Or allocated "
				"in startup static allocation?");
		}

		if(sTrackMallocInSection)
		{
			std::set<void *>::iterator si(sSectionMallocBlocks.find(ptr));
			if(si != sSectionMallocBlocks.end()) sSectionMallocBlocks.erase(si);
		}
	}

	//TRACE1("free(), %08x\n", ptr);
	std::free(ptr);
}


void memleakfinder_notaleak_insert_pre()
{
	InternalAllocGuard guard;

	if(!memleakfinder_global_enable) return;
	if(!memleakfinder_initialised)   return;

	for(size_t l = 0; l < sNotLeaksPreNum; l++)
	{
		sNotLeaks.insert(sNotLeaksPre[l]);
	}

	sNotLeaksPreNum = 0;
}

bool is_leak(void *ptr)
{
	InternalAllocGuard guard;

	ASSERT(memleakfinder_initialised);
	memleakfinder_notaleak_insert_pre();
	return sNotLeaks.find(ptr) == sNotLeaks.end();
}

void memleakfinder_notaleak(void *ptr)
{
	InternalAllocGuard guard;

	ASSERT(!sTrackingDataDestroyed);

	memleakfinder_notaleak_insert_pre();

	if(memleakfinder_initialised)
	{
		sNotLeaks.insert(ptr);
	}
	else
	{
		if ( sNotLeaksPreNum < 
			 sizeof(sNotLeaksPre)/sizeof(*sNotLeaksPre) )
			sNotLeaksPre[sNotLeaksPreNum++] = ptr;
	}

	/*
	{
		std::map<void *, MallocBlockInfo>::iterator i(sMallocBlocks.find(ptr));
		if(i != sMallocBlocks.end()) sMallocBlocks.erase(i);
	}
	{
		std::set<void *>::iterator si(sSectionMallocBlocks.find(ptr));
		if(si != sSectionMallocBlocks.end()) sSectionMallocBlocks.erase(si);
	}
	{
		std::map<void *, ObjectInfo>::iterator i(sObjectBlocks.find(ptr));
		if(i != sObjectBlocks.end()) sObjectBlocks.erase(i);
	}*/
}



// start monitoring a section of code
void memleakfinder_startsectionmonitor()
{
	InternalAllocGuard guard;

	ASSERT(memleakfinder_initialised);
	ASSERT(!sTrackingDataDestroyed);

	sTrackMallocInSection = true;
	sSectionMallocBlocks.clear();
	sTrackObjectsInSection = true;
	sSectionObjectBlocks.clear();
}

// trace all blocks allocated and still allocated since memleakfinder_startsectionmonitor() called
void memleakfinder_traceblocksinsection()
{
	InternalAllocGuard guard;

	ASSERT(memleakfinder_initialised);
	ASSERT(!sTrackingDataDestroyed);

	std::set<void *>::iterator s(sSectionMallocBlocks.begin());
	for(; s != sSectionMallocBlocks.end(); ++s)
	{
		std::map<void *, MallocBlockInfo>::const_iterator i(sMallocBlocks.find(*s));
		if(i == sMallocBlocks.end())
		{
			BOX_WARNING("Logical error in section block finding");
		}
		else
		{
			BOX_TRACE("Block " << i->first << " size " <<
				i->second.size << " allocated at " <<
				i->second.file << ":" << i->second.line);
		}
	}
	for(std::map<void *, ObjectInfo>::const_iterator i(sSectionObjectBlocks.begin()); i != sSectionObjectBlocks.end(); ++i)
	{
		BOX_TRACE("Object" << (i->second.array?" []":"") << " " <<
			i->first << " size " << i->second.size <<
			" allocated at " << i->second.file << 
			":" << i->second.line);
	}
}

int memleakfinder_numleaks()
{
	InternalAllocGuard guard;

	ASSERT(memleakfinder_initialised);
	ASSERT(!sTrackingDataDestroyed);

	int n = 0;
	
	for(std::map<void *, MallocBlockInfo>::const_iterator i(sMallocBlocks.begin()); i != sMallocBlocks.end(); ++i)
	{
		if(is_leak(i->first)) ++n;
	}
	
	for(std::map<void *, ObjectInfo>::const_iterator i(sObjectBlocks.begin()); i != sObjectBlocks.end(); ++i)
	{
		const ObjectInfo& rInfo = i->second;
		if(is_leak(i->first)) ++n;
	}

	return n;
}

// Summarise all blocks allocated and still allocated, for memory usage
// diagnostics.
void memleakfinder_report_usage_summary()
{
	InternalAllocGuard guard;

	ASSERT(!sTrackingDataDestroyed);

	typedef std::map<std::string, std::pair<uint64_t, uint64_t> > usage_map_t;
	usage_map_t usage;

	for(std::map<void *, MallocBlockInfo>::const_iterator
		i(sMallocBlocks.begin()); i != sMallocBlocks.end(); ++i)
	{
		std::ostringstream buf;
		buf << i->second.file << ":" << i->second.line;
		std::string key = buf.str();

		usage_map_t::iterator ui = usage.find(key);
		if(ui == usage.end())
		{
			usage[key] = std::pair<uint64_t, uint64_t>(1,
				i->second.size);
		}
		else
		{
			ui->second.first++;
			ui->second.second += i->second.size;
		}
	}

	for(std::map<void *, ObjectInfo>::const_iterator
		i(sObjectBlocks.begin()); i != sObjectBlocks.end(); ++i)
	{
		std::ostringstream buf;
		buf << i->second.file << ":" << i->second.line;
		std::string key = buf.str();

		usage_map_t::iterator ui = usage.find(key);
		if(ui == usage.end())
		{
			usage[key] = std::pair<uint64_t, uint64_t>(1,
				i->second.size);
		}
		else
		{
			ui->second.first++;
			ui->second.second += i->second.size;
		}
	}

	#ifndef DEBUG_LEAKS
		BOX_WARNING("Memory use: support not compiled in :(");
	#else
	if(usage.empty())
	{
		BOX_WARNING("Memory use: none detected?!");
	}
	else
	{
		uint64_t blocks = 0, bytes = 0;
		BOX_WARNING("Memory use: report follows");

		for(usage_map_t::iterator i = usage.begin(); i != usage.end();
			i++)
		{
			BOX_WARNING("Memory use: " << i->first << ": " <<
				i->second.first << " blocks, " <<
				i->second.second << " bytes");
			blocks += i->second.first;
			bytes  += i->second.second;
		}

		BOX_WARNING("Memory use: report ends, total: " << blocks <<
			" blocks, " << bytes << " bytes");
	}
	#endif // DEBUG_LEAKS
}

void memleakfinder_reportleaks_file(FILE *file)
{
	InternalAllocGuard guard;

	ASSERT(!sTrackingDataDestroyed);

	for(std::map<void *, MallocBlockInfo>::const_iterator
		i(sMallocBlocks.begin()); i != sMallocBlocks.end(); ++i)
	{
		if(is_leak(i->first))
		{
			::fprintf(file, "Block %p size %lu allocated at "
				"%s:%d\n", i->first, (unsigned long)i->second.size,
				i->second.file, i->second.line);
		}
	}

	for(std::map<void *, ObjectInfo>::const_iterator
		i(sObjectBlocks.begin()); i != sObjectBlocks.end(); ++i)
	{
		if(is_leak(i->first))
		{
			::fprintf(file, "Object%s %p size %lu allocated at "
				"%s:%d\n", i->second.array?" []":"",
				i->first, (unsigned long)i->second.size, i->second.file,
				i->second.line);
#ifdef BOX_MEMORY_LEAK_BACKTRACE_ENABLED
			if(file == stdout)
			{
				DumpStackBacktrace(__FILE__, i->second.stack_size,
					i->second.stack_frames);
			}
#endif
		}
	}
}

void memleakfinder_reportleaks()
{
	InternalAllocGuard guard;

	// report to stdout
	memleakfinder_reportleaks_file(stdout);
}

void memleakfinder_reportleaks_appendfile(const char *filename, const char *markertext)
{
	InternalAllocGuard guard;

	FILE *file = ::fopen(filename, "a");
	if(file != 0)
	{
		if(memleakfinder_numleaks() > 0)
		{
#ifdef HAVE_GETPID
			fprintf(file, "MEMORY LEAKS FROM PROCESS %d (%s)\n", getpid(), markertext);
#else
			fprintf(file, "MEMORY LEAKS (%s)\n", markertext);
#endif
			memleakfinder_reportleaks_file(file);
		}
	
		::fclose(file);
	}
	else
	{
		BOX_WARNING("Couldn't open memory leak results file " <<
			filename << " for appending");
	}
}

static char atexit_filename[512];
static char atexit_markertext[512];
static bool atexit_registered = false;

extern "C" void memleakfinder_atexit()
{
	memleakfinder_reportleaks_appendfile(atexit_filename, atexit_markertext);
}

void memleakfinder_setup_exit_report(const std::string& filename, 
	const char *markertext)
{
	char buffer[PATH_MAX];
	std::string abs_filename = std::string(getcwd(buffer, sizeof(buffer))) +
		DIRECTORY_SEPARATOR + filename;
	::strncpy(atexit_filename, abs_filename.c_str(),
		sizeof(atexit_filename)-1);
	::strncpy(atexit_markertext, markertext, sizeof(atexit_markertext)-1);
	atexit_filename[sizeof(atexit_filename)-1] = 0;
	atexit_markertext[sizeof(atexit_markertext)-1] = 0;
	if(!atexit_registered)
	{
		std::atexit(memleakfinder_atexit);
		atexit_registered = true;
	}
}

void add_object_block(void *block, size_t size, const char *file, int line, bool array)
{
	InternalAllocGuard guard;

	if(!memleakfinder_global_enable) return;
	if(!memleakfinder_initialised)   return;
	ASSERT(!sTrackingDataDestroyed);

	if(block != 0)
	{
		std::map<void *, ObjectInfo>::iterator j(sObjectBlocks.find(block));
		// The same block should not already be tracked!
		ASSERT(j == sObjectBlocks.end());

		ObjectInfo i;
		i.size = size;
		i.file = file;
		i.line = line;
		i.array = array;
#ifdef BOX_MEMORY_LEAK_BACKTRACE_ENABLED
		i.stack_size = backtrace(i.stack_frames, 20);
#endif
		sObjectBlocks[block] = i;
		
		if(sTrackObjectsInSection)
		{
			sSectionObjectBlocks[block] = i;
		}
	}
}

void remove_object_block(void *block)
{
	InternalAllocGuard guard;

	if(!memleakfinder_global_enable) return;
	if(!memleakfinder_initialised)   return;
	if(sTrackingDataDestroyed)       return;

	std::map<void *, ObjectInfo>::iterator i(sObjectBlocks.find(block));
	if(i != sObjectBlocks.end())
	{
		sObjectBlocks.erase(i);
	}

	if(sTrackObjectsInSection)
	{
		std::map<void *, ObjectInfo>::iterator i(sSectionObjectBlocks.find(block));
		if(i != sSectionObjectBlocks.end())
		{
			sSectionObjectBlocks.erase(i);
		}
	}

	// If it's not in the list, just ignore it, as lots of stuff goes this way...
}

static void *internal_new(size_t size, const char *file, int line)
{
	void *r;

	{
		InternalAllocGuard guard;
		r = std::malloc(size);
	}
	
	if (sInternalAllocDepth == 0)
	{
		InternalAllocGuard guard;
		add_object_block(r, size, file, line, false);
		//TRACE4("new(), %d, %s, %d, %08x\n", size, file, line, r);
	}

	return r;
}

void *operator new(size_t size, const char *file, int line)
{
	return internal_new(size, file, line);
}

void *operator new[](size_t size, const char *file, int line)
{
	return internal_new(size, file, line);
}

// where there is no doctor... need to override standard new() too
// http://www.relisoft.com/book/tech/9new.html
// disabled because it causes hangs on FC2 in futex() in test/common
// while reading files. reason unknown.
/*
void *operator new(size_t size)
{
	return internal_new(size, "standard libraries", 0);
}
*/

void *operator new[](size_t size) throw (std::bad_alloc)
{
	return internal_new(size, "standard libraries", 0);
}

void internal_delete(void *ptr)
{
	InternalAllocGuard guard;

	std::free(ptr);
	remove_object_block(ptr);
	//TRACE1("delete[]() called, %08x\n", ptr);
}

void operator delete[](void *ptr) throw ()
{
	internal_delete(ptr);
}

void operator delete(void *ptr) throw ()
{
	internal_delete(ptr);
}

/*
    We need to implement a placement operator delete too:

    "If the object is being created as part of a new expression, and an exception
    is thrown, the object’s memory is deallocated by calling the appropriate
    deallocation function. If the object is being created with a placement new
    operator, the corresponding placement delete operator is called—that is, the
    delete function that takes the same additional parameters as the placement new
    operator. If no matching placement delete is found, no deallocation takes
    place."

    So to avoid memory leaks, we need to implement placement delete operators that
    correspond to our placement new, which we use for leak detection (ironically)
    in debug builds.
*/

void operator delete(void *ptr, const char *file, int line)
{
	internal_delete(ptr);
}

void operator delete[](void *ptr, const char *file, int line)
{
	internal_delete(ptr);
}

#endif // BOX_MEMORY_LEAK_TESTING
