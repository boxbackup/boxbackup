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
//		Name:    DebugPrintf.cpp
//		Purpose: Implementation of a printf function, to avoid a stdio.h include in Box.h
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------

#ifndef NDEBUG

#include "Box.h"

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include "MemLeakFindOn.h"

// Use this apparently superflous printf function to avoid having to 
// include stdio.h in every file in the project.

int BoxDebug_printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int r = vprintf(format, ap);
	va_end(ap);
	return r;
}


bool BoxDebugTraceOn = true;
bool BoxDebugTraceToStdout = true;
bool BoxDebugTraceToSyslog = false;

int BoxDebugTrace(const char *format, ...)
{
	char text[512];
	int r = 0;
	if(BoxDebugTraceOn || BoxDebugTraceToSyslog)
	{
		va_list ap;
		va_start(ap, format);
		r = vsnprintf(text, sizeof(text), format, ap);
		va_end(ap);
	}

	// Send to stdout if trace is on and std out is enabled
	if(BoxDebugTraceOn && BoxDebugTraceToStdout)
	{
		printf("%s", text);		
	}

	// But tracing to syslog is independent of tracing being on or not
	if(BoxDebugTraceToSyslog)
	{
		// Remove trailing '\n', if it's there
		if(r > 0 && text[r] == '\n')
		{
			text[r] = '\0';
			--r;
		}
		// Log it
		::syslog(LOG_INFO, "TRACE: %s", text);
	}

	return r;
}


#endif // NDEBUG
