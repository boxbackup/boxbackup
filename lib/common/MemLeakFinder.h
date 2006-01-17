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

