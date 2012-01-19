// --------------------------------------------------------------------------
//
// File
//		Name:    Database.h
//		Purpose: Database (QDBM) utility macros
//		Created: 2010/03/10
//
// --------------------------------------------------------------------------

#ifndef DATABASE__H
#define DATABASE__H

#include "Logging.h"

#define BOX_DBM_MESSAGE(stuff) stuff << " (qdbm): " << dperrmsg(dpecode)

#define BOX_LOG_DBM_ERROR(stuff) \
	BOX_ERROR(BOX_DBM_MESSAGE(stuff))

#define THROW_DBM_ERROR(message, filename, exception, subtype) \
	BOX_LOG_DBM_ERROR(message << ": " << filename); \
	THROW_EXCEPTION_MESSAGE(exception, subtype, \
		BOX_DBM_MESSAGE(message << ": " << filename));

#define ASSERT_DBM_OK(operation, message, filename, exception, subtype) \
	if(!(operation)) \
	{ \
		THROW_DBM_ERROR(message, filename, exception, subtype); \
	}

#endif // DATABASE__H
