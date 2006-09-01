// --------------------------------------------------------------------------
//
// File
//		Name:    DbQuerySqlite.cpp
//		Purpose: Query object for Sqlite driver
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef PLATFORM_SQLITE3
	#include "sqlite3.h"
#else
	#include "sqlite.h"
#endif

#include "DbQuerySqlite.h"
#include "DbDriverSqlite.h"
#include "autogen_DatabaseException.h"
#include "Conversion.h"
#include "DbDriverInsertParameters.h"
#include "DbDriverSqliteV3.h"

#include "MemLeakFindOn.h"

// How to access a field
#define FIELD(column) (mppResults[((mCurrentRow+1)*mNumberColumns)+column])


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::DbQuerySqlite()
//		Purpose: Constructor
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DbQuerySqlite::DbQuerySqlite(DbDriverSqlite &rDriver)
	: mrDriver(rDriver),
	  mNumberRows(0),
	  mNumberColumns(0),
	  mppResults(0),
	  mCurrentRow(-1),
	  mNumberChanges(-1)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::~DbQuerySqlite()
//		Purpose: Destructor
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DbQuerySqlite::~DbQuerySqlite()
{
	if(mppResults != 0)
	{
		Finish();
	}
	ASSERT(mppResults == 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Execute(const char *, int, const Database::FieldType_t *, const void **)
//		Purpose: Execute a query
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbQuerySqlite::Execute(const char *SQLStatement, int NumberParameters,
	const Database::FieldType_t *pParameterTypes, const void **pParameters)
{
	if(mppResults != 0)
	{
		// Clean up the existing query object
		Finish();
	}

	// Run the query, returning all the results at once
	char **ppresults = 0;
	int nRows = 0, nColumns = 0;
	char *errmsg = 0;
	int returnCode = -1;
	// If there are parameters, then they need to be inserted into the SQL manually
	// by the driver -- there is no support in SQLite for this.
	if(NumberParameters != 0)
	{
		// Insert parameters, using the provided utility function
		std::string sql;
		DbDriverInsertParameters(SQLStatement, NumberParameters, pParameterTypes, pParameters, mrDriver, sql);
		// Execute it
		returnCode = ::sqlite_get_table(mrDriver.GetSqlite(), sql.c_str(), &ppresults, &nRows, &nColumns, &errmsg);
		if(returnCode != SQLITE_OK)
		{
			TRACE1("SQL was '%s'\n", sql.c_str());
		}
	}
	else
	{
		// Can just use this string
		returnCode = ::sqlite_get_table(mrDriver.GetSqlite(), SQLStatement, &ppresults, &nRows, &nColumns, &errmsg);
		if(returnCode != SQLITE_OK)
		{
			TRACE1("SQL was '%s'\n", SQLStatement);
		}
	}

	// Check return code
	if(returnCode != SQLITE_OK)
	{
		mrDriver.ReportErrorMessage(errmsg);
		THROW_EXCEPTION(DatabaseException, BadSQLStatement)
	}
	
	// Store the pointer to the returned data, and the size of the arrays
	mppResults = ppresults;
	mNumberRows = nRows;
	mNumberColumns = nColumns;
	mCurrentRow = -1;
	mNumberChanges = ::sqlite_changes(mrDriver.GetSqlite());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::GetNumberChanges()
//		Purpose: Return number of changes
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int DbQuerySqlite::GetNumberChanges() const
{
	return mNumberChanges;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::GetNumberRows()
//		Purpose: Get number of rows
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int DbQuerySqlite::GetNumberRows() const
{
	if(mppResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::GetNumberColumns()
//		Purpose: Get number of columns
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int DbQuerySqlite::GetNumberColumns() const
{
	if(mppResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberColumns;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::Next()
//		Purpose: Move cursor to next row
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
bool DbQuerySqlite::Next()
{
	if(mppResults == 0)
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
//		Name:    DbQuerySqlite::HaveRow()
//		Purpose: Row available?
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
bool DbQuerySqlite::HaveRow() const
{
	if(mppResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mCurrentRow >= 0 && mCurrentRow < mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::Finish()
//		Purpose: Finish with this query
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbQuerySqlite::Finish()
{
	// Check that this call is appropraite
	if(mppResults == 0)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Clean up
	::sqlite_free_table(mppResults);

	// All done
	mppResults = 0;
	mNumberRows = 0;
	mNumberColumns = 0;
	mCurrentRow = -1;
	mNumberChanges = -1;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::IsFieldNull(int)
//		Purpose: Is the field null?
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
bool DbQuerySqlite::IsFieldNull(int Column) const
{
	if(mppResults == 0)
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
	return FIELD(Column) == NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::GetFieldInt(int)
//		Purpose: Get a field's value, as int
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int32_t DbQuerySqlite::GetFieldInt(int Column) const
{
	if(mppResults == 0)
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

	// Do something vaguely sensible with null values
	if(FIELD(Column) == NULL)
	{
		return 0;
	}

	// Convert the number
	return BoxConvert::Convert<int32_t, const char *>(FIELD(Column));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQuerySqlite::GetFieldString(int, std::string &)
//		Purpose: Get a field's value, as string
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbQuerySqlite::GetFieldString(int Column, std::string &rStringOut) const
{
	if(mppResults == 0)
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

	// Do something vaguely sensible with null values
	if(FIELD(Column) == NULL)
	{
		rStringOut = std::string(); // ie clear();
		return;
	}

	// Copy string into output string
	rStringOut = FIELD(Column);
}

