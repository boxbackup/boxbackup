// --------------------------------------------------------------------------
//
// File
//		Name:    DbQueryMySQL.cpp
//		Purpose: Query object for MySQL driver
//		Created: 11/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "DbQueryMySQL.h"
#include "DbDriverMySQL.h"
#include "autogen_DatabaseException.h"
#include "Conversion.h"
#include "DbDriverInsertParameters.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::DbQueryMySQL()
//		Purpose: Constructor
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
DbQueryMySQL::DbQueryMySQL(DbDriverMySQL &rDriver)
	: mrDriver(rDriver),
	  mpResults(0),
	  mQueryReturnedData(true),
	  mChangedRows(-1),
	  mNumberRows(0),
	  mNumberColumns(0),
	  mFetchedFirstRow(false),
	  mCurrentRow(NULL),
	  mCurrentRowFieldLengths(NULL)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::~DbQueryMySQL()
//		Purpose: Destructor
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
DbQueryMySQL::~DbQueryMySQL()
{
	if(HaveResults())
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
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbQueryMySQL::Execute(const char *SQLStatement, int NumberParameters,
	const Database::FieldType_t *pParameterTypes, const void **pParameters)
{
	if(HaveResults())
	{
		// Clean up the existing query object
		Finish();
	}

	// If there are parameters, then they need to be inserted into the SQL manually
	// by the driver -- there is no support in MySQL for doing it quite how we require.
	int returnCode = -1;
	if(NumberParameters != 0)
	{
		// Insert parameters, using the provided utility function
		std::string sql;
		DbDriverInsertParameters(SQLStatement, NumberParameters, pParameterTypes, pParameters, mrDriver, sql);
		// Execute it
		returnCode = ::mysql_real_query(mrDriver.GetMYSQL(), sql.c_str(), sql.size());
	}
	else
	{
		// Can just use this string
		returnCode = ::mysql_real_query(mrDriver.GetMYSQL(), SQLStatement, ::strlen(SQLStatement));
	}

	// Worked?
	if(returnCode != 0)
	{
		mrDriver.ReportMySQLError();
		THROW_EXCEPTION(DatabaseException, BadSQLStatement)
	}

	// What kind of result is it?
	MYSQL_RES *presults = ::mysql_store_result(mrDriver.GetMYSQL());
	if(presults == NULL)
	{
		// No results returned -- should it have done?
		if(::mysql_field_count(mrDriver.GetMYSQL()) == 0)
		{
			// No data should be returned (not a SELECT)
			// Instead, find out how much data was returned
			my_ulonglong r = ::mysql_affected_rows(mrDriver.GetMYSQL());
			if(r == ((my_ulonglong)-1))
			{
				// Error
				mrDriver.ReportMySQLError();
				THROW_EXCEPTION(DatabaseException, ErrorExecutingSQL)
			}
			mQueryReturnedData = false;
			mChangedRows = r;
		}
		else
		{
			// Data should have been returned
			mrDriver.ReportMySQLError();
			THROW_EXCEPTION(DatabaseException, ErrorExecutingSQL)
		}
	}
	else
	{
		// Have results, store them
		mQueryReturnedData = true;
		mpResults = presults;
		mFetchedFirstRow = false;
		
		// Find out a bit more data
		mNumberColumns = ::mysql_num_fields(mpResults);
		mNumberRows = ::mysql_num_rows(mpResults);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::GetNumberChanges()
//		Purpose: Return number of changes
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
int DbQueryMySQL::GetNumberChanges() const
{
	if(!HaveResults())
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
//		Name:    DbQueryMySQL::GetNumberRows()
//		Purpose: Get number of rows
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
int DbQueryMySQL::GetNumberRows() const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberRows;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::GetNumberColumns()
//		Purpose: Get number of columns
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
int DbQueryMySQL::GetNumberColumns() const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mNumberColumns;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::Next()
//		Purpose: Move cursor to next row
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
bool DbQueryMySQL::Next()
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Emulate zero rows behaviour for queries which didn't return data
	if(!mFetchedFirstRow && !mQueryReturnedData)
	{
		mFetchedFirstRow = true;
		return false;
	}

	// Check that a row should be fetched
	if(mFetchedFirstRow && mCurrentRow == NULL)
	{
		THROW_EXCEPTION(DatabaseException, AttemptToMoveBeyondQueryEnd)
	}

	ASSERT(mpResults != NULL);

	// Try to get a row
	mCurrentRow = ::mysql_fetch_row(mpResults);
	mFetchedFirstRow = true;

	// If a row was returned, get the lengths of all the fields returned
	if(mCurrentRow != NULL)
	{
		mCurrentRowFieldLengths = ::mysql_fetch_lengths(mpResults);
		if(mCurrentRowFieldLengths == NULL)
		{
			THROW_EXCEPTION(DatabaseException, UnexpectedLibraryBehaviour)
		}
	}

	// Got a row?
	return mCurrentRow != NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::HaveRow()
//		Purpose: Row available?
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
bool DbQueryMySQL::HaveRow() const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	return mCurrentRow != NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::Finish()
//		Purpose: Finish with this query
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbQueryMySQL::Finish()
{
	// Check that this call is appropraite
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Free results?
	if(mpResults != 0)
	{
		::mysql_free_result(mpResults);
		mpResults = 0;
	}

	// Reset data
	mpResults = 0;
	mQueryReturnedData = true;
	mChangedRows = -1;
	mNumberRows = 0;
	mNumberColumns = 0;
	mFetchedFirstRow = false;
	mCurrentRow = NULL;
	mCurrentRowFieldLengths = NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::IsFieldNull(int)
//		Purpose: Is the field null?
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
bool DbQueryMySQL::IsFieldNull(int Column) const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == NULL && !mFetchedFirstRow)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow == NULL)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Simple check
	return mCurrentRow[Column] == NULL;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::GetFieldInt(int)
//		Purpose: Get a field's value, as int
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
int32_t DbQueryMySQL::GetFieldInt(int Column) const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == NULL && !mFetchedFirstRow)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow == NULL)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Do something vaguely sensible with null values
	if(mCurrentRow[Column] == NULL)
	{
		return 0;
	}

	// Convert the number
	return BoxConvert::Convert<int32_t, const char *>(mCurrentRow[Column]);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbQueryMySQL::GetFieldString(int, std::string &)
//		Purpose: Get a field's value, as string
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbQueryMySQL::GetFieldString(int Column, std::string &rStringOut) const
{
	if(!HaveResults())
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mCurrentRow == NULL && !mFetchedFirstRow)
	{
		THROW_EXCEPTION(DatabaseException, MustCallNextBeforeReadingData)
	}
	if(mCurrentRow == NULL)
	{
		THROW_EXCEPTION(DatabaseException, NoRowAvailable)
	}
	if(Column < 0 || Column >= mNumberColumns)
	{
		THROW_EXCEPTION(DatabaseException, ColumnOutOfRange)
	}

	// Do something vaguely sensible with null values
	if(mCurrentRow[Column] == NULL)
	{
		rStringOut = std::string(); // ie clear();
		return;
	}

	// Copy string into output string
	rStringOut.assign(mCurrentRow[Column], mCurrentRowFieldLengths[Column]);
}

