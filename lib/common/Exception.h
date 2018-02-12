// --------------------------------------------------------------------------
//
// File
//		Name:    Exception.h
//		Purpose: Macros for throwing exceptions
//		Created: 2018/02/12
//
// --------------------------------------------------------------------------

#ifndef EXCEPTION__H
#define EXCEPTION__H

#include <exception>
#include <string>

#include "CommonException.h"
#include "Logging.h"

// Show backtraces on exceptions in release builds until further notice
// (they are only logged at TRACE level anyway)
#if defined WIN32 || defined HAVE_EXECINFO_H
	#define SHOW_BACKTRACE_ON_EXCEPTION
#endif

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	#include "Utils.h"
	#define OPTIONAL_DO_BACKTRACE DumpStackBacktrace(__FILE__);
#else
	#define OPTIONAL_DO_BACKTRACE
#endif

#define THROW_EXCEPTION(type, subtype) \
	{ \
		if((!HideExceptionMessageGuard::ExceptionsHidden() \
			&& !HideSpecificExceptionGuard::IsHidden( \
				type::ExceptionType, type::subtype))) \
		{ \
			OPTIONAL_DO_BACKTRACE \
			BOX_WARNING("Exception thrown: " \
				#type "(" #subtype ") " \
				"at " __FILE__ "(" << __LINE__ << ")") \
		} \
		throw type(type::subtype); \
	}

#define THROW_EXCEPTION_MESSAGE(type, subtype, message) \
	{ \
		std::ostringstream _box_throw_line; \
		_box_throw_line << message; \
		if((!HideExceptionMessageGuard::ExceptionsHidden() \
			&& !HideSpecificExceptionGuard::IsHidden( \
				type::ExceptionType, type::subtype))) \
		{ \
			OPTIONAL_DO_BACKTRACE \
			BOX_WARNING("Exception thrown: " \
				#type "(" #subtype ") (" << \
				_box_throw_line.str() << \
				") at " __FILE__ ":" << __LINE__) \
		} \
		throw type(type::subtype, _box_throw_line.str()); \
	}

#ifndef BOX_RELEASE_BUILD
	void BoxDebugAssertFailed(const char *cond, const char *file, int line);
	#define ASSERT(cond) \
	{ \
		if(!(cond)) \
		{ \
			BoxDebugAssertFailed(#cond, __FILE__, __LINE__); \
			THROW_EXCEPTION_MESSAGE(CommonException, \
				AssertFailed, #cond); \
		} \
	}

	// Exception names
	#define EXCEPTION_CODENAMES_EXTENDED
#else
	#define ASSERT(cond)

	// Box Backup builds release get extra information for exception logging
	#define EXCEPTION_CODENAMES_EXTENDED
	#define EXCEPTION_CODENAMES_EXTENDED_WITH_DESCRIPTION
#endif

#endif // BOXEXCEPTION__H

