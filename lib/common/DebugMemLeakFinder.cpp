// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    MemLeakFinder.cpp
//		Purpose: Memory leak finder
//		Created: 12/1/04
//
// --------------------------------------------------------------------------


#ifndef NDEBUG

#include "Box.h"

#undef malloc
#undef realloc
#undef free

#include <map>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <set>

bool memleakfinder_global_enable = false;

typedef struct
{
	size_t size;
	const char *file;
	int line;
} MallocBlockInfo;

typedef struct
{
	size_t size;
	const char *file;
	int line;
	bool array;
} ObjectInfo;

namespace
{
	static std::map<void *, MallocBlockInfo> sMallocBlocks;
	static std::map<void *, ObjectInfo> sObjectBlocks;
	
	static bool sTrackMallocInSection = false;
	static std::set<void *> sSectionMallocBlocks;
	static bool sTrackObjectsInSection = false;
	static std::map<void *, ObjectInfo> sSectionObjectBlocks;
	
	static std::set<void *> sNotLeaks;

	void *sNotLeaksPre[1024];
	int sNotLeaksPreNum = 0;
}

void memleakfinder_malloc_add_block(void *b, size_t size, const char *file, int line)
{
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
	void *b = ::malloc(size);
	if(!memleakfinder_global_enable) return b;

	memleakfinder_malloc_add_block(b, size, file, line);

	//TRACE4("malloc(), %d, %s, %d, %08x\n", size, file, line, b);
	return b;
}

void *memleakfinder_realloc(void *ptr, size_t size)
{
	if(!memleakfinder_global_enable)
	{
		return ::realloc(ptr, size);
	}

	// Check it's been allocated
	std::map<void *, MallocBlockInfo>::iterator i(sMallocBlocks.find(ptr));
	if(i == sMallocBlocks.end())
	{
		TRACE1("Block %x realloc(), but not in list. Error? Or allocated in startup static objects?\n", ptr);
		void *b = ::realloc(ptr, size);
		memleakfinder_malloc_add_block(b, size, "FOUND-IN-REALLOC", 0);
		return b;
	}

	void *b = ::realloc(ptr, size);
	
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
	
	//TRACE3("realloc(), %d, %08x->%08x\n", size, ptr, b);
	return b;
}

void memleakfinder_free(void *ptr)
{
	if(memleakfinder_global_enable)
	{
		// Check it's been allocated
		std::map<void *, MallocBlockInfo>::iterator i(sMallocBlocks.find(ptr));
		if(i != sMallocBlocks.end())
		{
			sMallocBlocks.erase(i);
		}
		else
		{
			TRACE1("Block %x freed, but not known. Error? Or allocated in startup static allocation?\n", ptr);
		}

		if(sTrackMallocInSection)
		{
			std::set<void *>::iterator si(sSectionMallocBlocks.find(ptr));
			if(si != sSectionMallocBlocks.end()) sSectionMallocBlocks.erase(si);
		}
	}

	//TRACE1("free(), %08x\n", ptr);
	::free(ptr);
}


void memleakfinder_notaleak_insert_pre()
{
	if(!memleakfinder_global_enable) return;
	for(int l = 0; l < sNotLeaksPreNum; l++)
	{
		sNotLeaks.insert(sNotLeaksPre[l]);
	}
	sNotLeaksPreNum = 0;
}

bool is_leak(void *ptr)
{
	memleakfinder_notaleak_insert_pre();
	return sNotLeaks.find(ptr) == sNotLeaks.end();
}

void memleakfinder_notaleak(void *ptr)
{
	memleakfinder_notaleak_insert_pre();
	if(memleakfinder_global_enable)
	{
		sNotLeaks.insert(ptr);
	}
	else
	{
		sNotLeaksPre[sNotLeaksPreNum++] = ptr;
	}
/*	{
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
	sTrackMallocInSection = true;
	sSectionMallocBlocks.clear();
	sTrackObjectsInSection = true;
	sSectionObjectBlocks.clear();
}

// trace all blocks allocated and still allocated since memleakfinder_startsectionmonitor() called
void memleakfinder_traceblocksinsection()
{
	std::set<void *>::iterator s(sSectionMallocBlocks.begin());
	for(; s != sSectionMallocBlocks.end(); ++s)
	{
		std::map<void *, MallocBlockInfo>::const_iterator i(sMallocBlocks.find(*s));
		if(i == sMallocBlocks.end())
		{
			TRACE0("Logical error in section block finding\n");
		}
		else
		{
			TRACE4("Block 0x%08x size %d allocated at %s:%d\n", (int)i->first, i->second.size, i->second.file, i->second.line);
		}
	}
	for(std::map<void *, ObjectInfo>::const_iterator i(sSectionObjectBlocks.begin()); i != sSectionObjectBlocks.end(); ++i)
	{
		TRACE5("Object%s 0x%08x size %d allocated at %s:%d\n", i->second.array?" []":"", (int)i->first, i->second.size, i->second.file, i->second.line);
	}
}

int memleakfinder_numleaks()
{
	int n = 0;
	
	for(std::map<void *, MallocBlockInfo>::const_iterator i(sMallocBlocks.begin()); i != sMallocBlocks.end(); ++i)
	{
		if(is_leak(i->first)) ++n;
	}
	
	for(std::map<void *, ObjectInfo>::const_iterator i(sObjectBlocks.begin()); i != sObjectBlocks.end(); ++i)
	{
		if(is_leak(i->first)) ++n;
	}

	return n;
}

void memleakfinder_reportleaks_file(FILE *file)
{
	for(std::map<void *, MallocBlockInfo>::const_iterator i(sMallocBlocks.begin()); i != sMallocBlocks.end(); ++i)
	{
		if(is_leak(i->first)) ::fprintf(file, "Block 0x%08x size %d allocated at %s:%d\n", (int)i->first, i->second.size, i->second.file, i->second.line);
	}
	for(std::map<void *, ObjectInfo>::const_iterator i(sObjectBlocks.begin()); i != sObjectBlocks.end(); ++i)
	{
		if(is_leak(i->first)) ::fprintf(file, "Object%s 0x%08x size %d allocated at %s:%d\n", i->second.array?" []":"", (int)i->first, i->second.size, i->second.file, i->second.line);
	}
}

void memleakfinder_reportleaks()
{
	// report to stdout
	memleakfinder_reportleaks_file(stdout);
}

void memleakfinder_reportleaks_appendfile(const char *filename, const char *markertext)
{
	FILE *file = ::fopen(filename, "a");
	if(file != 0)
	{
		if(memleakfinder_numleaks() > 0)
		{
			fprintf(file, "MEMORY LEAKS FROM PROCESS %d (%s)\n", getpid(), markertext);
			memleakfinder_reportleaks_file(file);
		}
	
		::fclose(file);
	}
	else
	{
		printf("WARNING: Couldn't open memory leak results file %s for appending\n", filename);
	}
}

static char atexit_filename[512];
static char atexit_markertext[512];
static bool atexit_registered = false;

void memleakfinder_atexit()
{
	memleakfinder_reportleaks_appendfile(atexit_filename, atexit_markertext);
}

void memleakfinder_setup_exit_report(const char *filename, const char *markertext)
{
	::strcpy(atexit_filename, filename);
	::strcpy(atexit_markertext, markertext);
	if(!atexit_registered)
	{
		atexit(memleakfinder_atexit);
		atexit_registered = true;
	}
}




void add_object_block(void *block, size_t size, const char *file, int line, bool array)
{
	if(!memleakfinder_global_enable) return;

	if(block != 0)
	{
		ObjectInfo i;
		i.size = size;
		i.file = file;
		i.line = line;
		i.array = array;
		sObjectBlocks[block] = i;
		
		if(sTrackObjectsInSection)
		{
			sSectionObjectBlocks[block] = i;
		}
	}
}

void remove_object_block(void *block)
{
	if(!memleakfinder_global_enable) return;

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

void *operator new(size_t size, const char *file, int line)
{
	void *r = ::malloc(size);
	add_object_block(r, size, file, line, false);
	//TRACE4("new(), %d, %s, %d, %08x\n", size, file, line, r);
	return r;
}

void *operator new[](size_t size, const char *file, int line)
{
	void *r = ::malloc(size);
	add_object_block(r, size, file, line, true);
	//TRACE4("new[](), %d, %s, %d, %08x\n", size, file, line, r);
	return r;
}

void operator delete[](void *ptr) throw ()
{
	::free(ptr);
	remove_object_block(ptr);
	//TRACE1("delete[]() called, %08x\n", ptr);
}

void operator delete(void *ptr) throw ()
{
	::free(ptr);
	remove_object_block(ptr);
	//TRACE1("delete() called, %08x\n", ptr);
}

#endif // NDEBUG
