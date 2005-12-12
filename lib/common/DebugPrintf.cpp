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

#ifdef WIN32
	#include "emu.h"
#else
	#include <syslog.h>
#endif

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
#ifdef WIN32		
		// Remove trailing '\n', if it's there
		if(r > 0 && text[r-1] == '\n')
		{
			text[r-1] = '\0';
#else
		if(r > 0 && text[r] == '\n')
		{
			text[r] = '\0';
#endif
			--r;
		}
		// Log it
		::syslog(LOG_INFO, "TRACE: %s", text);
	}

	return r;
}


#endif // NDEBUG
