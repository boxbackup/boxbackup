// --------------------------------------------------------------------------
//
// File
//		Name:    DbQueryPostgreSQL.cpp
//		Purpose: Query object for PostgreSQL driver
//		Created: 26/12/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>
#include <limits.h>
#include <arpa/inet.h>

#include "DbQueryPostgreSQL.h"
#include "DbDriverPostgreSQL.h"
#include "autogen_DatabaseException.h"
#include "Conversion.h"
#include "DbDriverInsertParameters.h"

// Really, postgres/catalog/pg_type.h should be included, not this.
// However, it's difficult to include on all platforms, so instead,
// a file generated (by manually running script) from the header file.
// OID types really shouldn't change, so this won't be a problem.
// (Hopefully.) But the database test checks the types as well.
#include "PostgreSQLOidTypes.h"

// Get the numeric type definition and the conversion functions
extern "C"
{
	#include "postgresql/pgtypes_numeric.h"
}

#include "MemLeakFindOn.h"


// With the binary protocol, is byte swapping required?
#ifndef PLATFORM_POSTGRESQL_OLD_API
	#if BYTE_ORDER != BIG_ENDIAN
		#define PG_BYTE_SWAPPING_REQUIRED
	#endif
#endif


// PostgreSQL headers don't define constants like this
#define FORMAT_TEXT		0
#define FORMAT_BINARY	1

// When using the new API, should results be requested in text or binary form?
#ifdef POSTGRESQL_QUERY_RESULT_FORMAT
	// Allow extra command line args to adjust behaviour
	#define QUERY_RESULT_FORMAT		POSTGRESQL_QUERY_RESULT_FORMAT
#else
	// By default, use binary format
	#define QUERY_RESULT_FORMAT		FORMAT_BINARY
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::DbQueryPostgreSQL()
//		Purpose: Constructor
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
DbQueryPostgreSQL::DbQueryPostgreSQL(DbDriverPostgreSQL &rDriver)
	: mrDriver(rDriver),
	  mpResults(0),
	  mQueryReturnedData(true),
	  mChangedRows(-1),
	  mNumberRows(0),
	  mNumberColumns(0),
	  mCurrentRow(-1)
{
	// Check a few things are the right size and value
	ASSERT(sizeof(Database::FieldType_t) == sizeof(Oid));
	ASSERT(Database::Type_String == TEXTOID);
	ASSERT(Database::Type_Int32 == INT4OID);
	ASSERT(Database::Type_Int16 == INT2OID);
#ifndef NDEBUG
	static bool displayedConfig = false;
	if(!displayedConfig)
	{
		displayedConfig = true;
#ifdef PLATFORM_POSTGRESQL_OLD_API
		TRACE0("DbQueryPostgreSQL configured to use old API\n");
#else
		TRACE1("DbQueryPostgreSQL configured to use new API with results in %s mode\n",
				(QUERY_RESULT_FORMAT == FORMAT_BINARY)?"binary":"text");
#endif
#ifdef PG_BYTE_SWAPPING_REQUIRED
		TRACE0("DbQueryPostgreSQL byte swapping on binary data code enabled\n");
#endif
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::~DbQueryPostgreSQL()
//		Purpose: Destructor
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
DbQueryPostgreSQL::~DbQueryPostgreSQL()
{
	if(mpResults != 0)
	{
		Finish();
	}
	ASSERT(mpResults == 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Execute(const char *, int, const Database::FieldType_t *, const void **)
//		Purpose: Execute a query
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbQueryPostgreSQL::Execute(const char *SQLStatement, int NumberParameters,
	const Database::FieldType_t *pParameterTypes, const void **pParameters)
{
	if(mpResults != 0)
	{
		// Clean up the existing query object
		Finish();
	}

	// Redundant check -- DatabaseQuery::Execute() will check this, but it's really important
	// that this isn't exceeded here.
	if(NumberParameters > Database::MaxParameters)
	{
		THROW_EXCEPTION(DatabaseException, TooManyParameters)
	}

	// TODO: Use PQexecParams instead!
	PGresult *presults = NULL;
	if(NumberParameters != 0)
	{
#ifdef PLATFORM_POSTGRESQL_OLD_API
		// Insert parameters, using the provided utility function
		std::string sql;
		DbDriverInsertParameters(SQLStatement, NumberParameters, pParameterTypes, pParameters, mrDriver, sql);
		// Execute it
		presults = ::PQexec(mrDriver.GetPGconn(), sql.c_str());
#else
		// Build list of types
		int paramLengths[Database::MaxParameters];
		int paramFormats[Database::MaxParameters];
#ifdef PG_BYTE_SWAPPING_REQUIRED
		const void *paramPointers[Database::MaxParameters];
		int32_t paramIntegers[Database::MaxParameters];
#endif
		for(int p = 0; p < NumberParameters; ++p)
		{
			switch(pParameterTypes[p])
			{
			case Database::Type_String:
				paramLengths[p] = 0;
				paramFormats[p] = FORMAT_TEXT;
#ifdef PG_BYTE_SWAPPING_REQUIRED
				// Just use supplied pointer
				paramPointers[p] = pParameters[p];
#endif
				break;
			case Database::Type_Int32:
				paramLengths[p] = (pParameters[p] == NULL)?0:4;
				paramFormats[p] = FORMAT_BINARY;
#ifdef PG_BYTE_SWAPPING_REQUIRED
				// Copy integer, adjust pointer
				{
					int32_t i = *((int32_t*)(pParameters[p]));
					paramIntegers[p] = htonl(i);
					paramPointers[p] = paramIntegers + p;
				}
#endif
				break;
			case Database::Type_Int16:
				paramLengths[p] = (pParameters[p] == NULL)?0:2;
				paramFormats[p] = FORMAT_BINARY;
#ifdef PG_BYTE_SWAPPING_REQUIRED
				// Copy integer, adjust pointer
				{
					int16_t i = *((int16_t*)(pParameters[p]));
					int16_t *pint = (int16_t*)(paramIntegers + p);
					*pint = htons(i);
					paramPointers[p] = pint;
				}
#endif
				break;
			default:
				THROW_EXCEPTION(DatabaseException, UnknownValueType)
				break;
			}
		}

		// Execute using binary protocol to insert parameters without having to quote strings
		presults = ::PQexecParams(mrDriver.GetPGconn(), SQLStatement, NumberParameters,
			(const Oid *)pParameterTypes,		// can do this, because the type numbers are chosen carefully
#ifdef PG_BYTE_SWAPPING_REQUIRED
			(const char * const *)paramPointers,
#else
			(const char * const *)pParameters,
#endif
			paramLengths, paramFormats, QUERY_RESULT_FORMAT);
#endif
	}
	else
	{
		// Can just use this string
#ifdef PLATFORM_POSTGRESQL_OLD_API
		presults = ::PQexec(mrDriver.GetPGconn(), SQLStatement);
#else
		presults = ::PQexecParams(mrDriver.GetPGconn(), SQLStatement, 0, NULL, NULL, NULL, NULL, QUERY_RESULT_FORMAT);
#endif
	}

	// Worked?
	if(presults == NULL)
	{
		mrDriver.ReportPostgreSQLError();
		THROW_EXCEPTION(DatabaseException, ErrorExecutingSQL)
	}
	
	// Check what actually happened
	switch(::PQresultStatus(presults))
	{
	case PGRES_TUPLES_OK:
		// Command returned some data
		{
			mQueryReturnedData = true;
			mNumberColumns = ::PQnfields(presults);
			mNumberRows = ::PQntuples(presults);
			// Store pointer to results
			mpResults = presults;
		}
		break;

	case PGRES_COMMAND_OK:
		// Command completed, and didn't return anything
		{
			mQueryReturnedData = false;
			// Find the number of changed rows
			const char *changed = ::PQcmdTuples(presults);
			if(changed == NULL || changed[0] == '\0')
			{
				mChangedRows = 0;
			}
			else
			{
				mChangedRows = BoxConvert::Convert<int32_t, const char *>(changed);
			}
			// Store pointer to results
			mpResults = presults;
		}
		break;

	case PGRES_EMPTY_QUERY:
		// Empty SQL statements aren't allowed in this interface, consider it a bad SQL statement
		{
			::PQclear(presults);
			THROW_EXCEPTION(DatabaseException, BadSQLStatement)
		}
		break;

	case PGRES_NONFATAL_ERROR:
	case PGRES_FATAL_ERROR:
		{
			mrDriver.ReportPostgreSQLError();
			::PQclear(presults);
			THROW_EXCEPTION(DatabaseException, ErrorExecutingSQL)
		}
		break;

	default:
		{
			::PQclear(presults);
			THROW_EXCEPTION(DatabaseException, UnexpectedLibraryBehaviour)
		}
		break;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::GetNumberChanges()
//		Purpose: Return number of changes
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
int DbQueryPostgreSQL::GetNumberChanges() const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	if(mQueryReturnedData)
	{
		// No rows changed
		return 0;
	}
	
	return mChangedRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::GetNumberRows()
//		Purpose: Get number of rows
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
int DbQueryPostgreSQL::GetNumberRows() const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::GetNumberColumns()
//		Purpose: Get number of columns
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
int DbQueryPostgreSQL::GetNumberColumns() const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberColumns;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::Next()
//		Purpose: Move cursor to next row
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
bool DbQueryPostgreSQL::Next()
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Check that the caller isn't attempting to advance when the end has already been found
	if(mCurrentRow >= mNumberRows)
	{
		THROW_EXCEPTION(DatabaseException, AttemptToMoveBeyondQueryEnd)
	}

	// Advance to next row
	++mCurrentRow;

	// Got a row?
	return mCurrentRow < mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::HaveRow()
//		Purpose: Row available?
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
bool DbQueryPostgreSQL::HaveRow() const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mCurrentRow >= 0 && mCurrentRow < mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::Finish()
//		Purpose: Finish with this query
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbQueryPostgreSQL::Finish()
{
	// Check that this call is appropraite
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Free results
	::PQclear(mpResults);

	// Reset data
	mpResults = 0;
	mQueryReturnedData = true;
	mChangedRows = -1;
	mNumberRows = 0;
	mNumberColumns = 0;
	mCurrentRow = -1;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::IsFieldNull(int)
//		Purpose: Is the field null?
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
bool DbQueryPostgreSQL::IsFieldNull(int Column) const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == -1)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow >= mNumberRows)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Simple check
	return ::PQgetisnull(mpResults, mCurrentRow, Column) == 1;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::GetFieldInt(int)
//		Purpose: Get a field's value, as int
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
int32_t DbQueryPostgreSQL::GetFieldInt(int Column) const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == -1)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow >= mNumberRows)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Get value
	const char *value = ::PQgetvalue(mpResults, mCurrentRow, Column);

	// Do something vaguely sensible with null values
	// NOTE: a NULL pointer should never be returned according to the docs, but be paranoid
	if(value == NULL)
	{
		return 0;
	}

#ifdef PLATFORM_POSTGRESQL_OLD_API
	// Convert the number
	if(value[0] == '\0') return 0;
	return BoxConvert::Convert<int32_t, const char *>(value);
#else
	// ------- NEW API -------
	if(::PQgetisnull(mpResults, mCurrentRow, Column))
	{
		// Handle nulls gracefully.
		return 0;
	}
	switch(::PQfformat(mpResults, Column))
	{
	case FORMAT_TEXT:
		if(value[0] == '\0') return 0;
		return BoxConvert::Convert<int32_t, const char *>(value);
		break;

	case FORMAT_BINARY:
		switch(::PQftype(mpResults, Column))
		{
		#define CASE_PG_STRING_TYPES \
		case NAMEOID: \
		case TEXTOID: \
		case VARCHAROID: \
		case BPCHAROID:
		CASE_PG_STRING_TYPES
			if(value[0] == '\0') return 0;
			return BoxConvert::Convert<int32_t, const char *>(value);
			break;

		// Binary numeric types
		case BOOLOID:	// Single byte types
		case CHAROID:
			return value[0];
			break;

		case INT2OID:	// 2 byte types
#ifdef PG_BYTE_SWAPPING_REQUIRED
			return ntohs(*((int16_t*)value));
#else
			return *((int16_t*)value);
#endif
			break;

		case INT4OID:	// 4 byte types
		case OIDOID:
#ifdef PG_BYTE_SWAPPING_REQUIRED
			return ntohl(*((int32_t*)value));
#else
			return *((int32_t*)value);
#endif
			break;

		case INT8OID:	// 8 byte types
			{
#ifdef PG_BYTE_SWAPPING_REQUIRED
				int64_t v = ntoh64(*((int64_t*)value));
#else
				int64_t v = *((int64_t*)value);
#endif
				if(v > LONG_MAX || v < LONG_MIN)
				{
					// Overflow
					THROW_EXCEPTION(DatabaseException, IntegerOverflow)
				}
				// Cast down
				return (int32_t)v;
			}
			break;

		case NUMERICOID:
			{
				numeric *num = (numeric*)value;
				long v = 0;
				if(::PGTYPESnumeric_to_long(num, &v) != 0)
				{
					THROW_EXCEPTION(DatabaseException, PostgreSQLBadConversionFromNumericType)		
				}
				return v;
			}
			break;

		default:
			TRACE1("Unhandled pg type is %d\n", ::PQftype(mpResults, Column));
			THROW_EXCEPTION(DatabaseException, PostgreSQLUnhandledBinaryType)
			break;
		}
		break;

	default:
		// Other values are not defined
		THROW_EXCEPTION(DatabaseException, UnexpectedLibraryBehaviour)
		break;
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryPostgreSQL::GetFieldString(int, std::string &)
//		Purpose: Get a field's value, as string
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbQueryPostgreSQL::GetFieldString(int Column, std::string &rStringOut) const
{
	if(mpResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == -1)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow >= mNumberRows)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Get value
	const char *value = ::PQgetvalue(mpResults, mCurrentRow, Column);

	// Do something vaguely sensible with null values (not that this should ever really happen)
	if(value == NULL)
	{
		rStringOut = std::string(); // ie clear();
		return;
	}

#ifdef PLATFORM_POSTGRESQL_OLD_API
	// Copy string into output string
	rStringOut = value;
#else
	// ------- NEW API -------
	if(::PQgetisnull(mpResults, mCurrentRow, Column))
	{
		// Handle nulls gracefully.
		rStringOut = "";
		return;
	}
	switch(::PQfformat(mpResults, Column))
	{
	case FORMAT_TEXT:
		rStringOut = value;
		break;

	case FORMAT_BINARY:
		switch(::PQftype(mpResults, Column))
		{
		CASE_PG_STRING_TYPES
			rStringOut = value;
			break;
		default:
			THROW_EXCEPTION(DatabaseException, PostgreSQLUnhandledBinaryType)
			break;
		}
		break;

	default:
		// Other values are not defined
		THROW_EXCEPTION(DatabaseException, UnexpectedLibraryBehaviour)
		break;
	}
#endif
}

