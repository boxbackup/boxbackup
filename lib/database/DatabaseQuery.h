// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseQuery.h
//		Purpose: Database query abstraction
//		Created: 1/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASEQUERY__H
#define DATABASEQUERY__H

#include <string>

#include "DatabaseDriver.h"

class DatabaseConnection;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DatabaseQuery
//		Purpose: Database query abstraction
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
class DatabaseQuery
{
public:
	DatabaseQuery(DatabaseConnection &rConnection);
	virtual ~DatabaseQuery();
private:
	// No copying
	DatabaseQuery(const DatabaseQuery &);
	DatabaseQuery &operator=(const DatabaseQuery &);
public:

	// The derived class will provide some nicer methods to call
	void Execute(int NumberParameters, const Database::FieldType_t *pParameterTypes, const void **pParameters);

	void Finish();

	// Inline implementation of various functions which call an internal "driver query".
	// It's safe to just call the driver query via the pointer, as there will either be a valid pointer,
	// or a pointer to the special null query class.

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::GetNumberChanges()
	//		Purpose: Return the number of rows which were changed by the last query.
	//		Created: 10/5/04
	//
	// --------------------------------------------------------------------------
	int GetNumberChanges() const {return mpQuery->GetNumberChanges();}


	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::GetNumberRows()
	//		Purpose: Return the number of rows in the result set
	//		Created: 10/5/04
	//
	// --------------------------------------------------------------------------
	int GetNumberRows() const {return mpQuery->GetNumberRows();}


	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::GetNumberColumns()
	//		Purpose: Return the number of columns in the result set
	//		Created: 10/5/04
	//
	// --------------------------------------------------------------------------
	int GetNumberColumns() const {return mpQuery->GetNumberColumns();}

	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::Next()
	//		Purpose: Move the query to the next row, returning true if there is
	//				 a row to retrieve after moving forward. (ie HaveRow() will
	//				 return true as well)
	//		Created: 7/5/04
	//
	// --------------------------------------------------------------------------
	bool Next() {return mpQuery->Next();}


	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::HaveRow()
	//		Purpose: Is there a row of data available
	//		Created: 7/5/04
	//
	// --------------------------------------------------------------------------
	bool HaveRow() const {return mpQuery->HaveRow();}


	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::IsFieldNull(int)
	//		Purpose: Return true if the nth field is null
	//		Created: 8/5/04
	//
	// --------------------------------------------------------------------------
	bool IsFieldNull(int Column) const {return mpQuery->IsFieldNull(Column);}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::GetField[Int|String](int[, reference])
	//		Purpose: Get the nth field as the named type
	//		Created: 7/5/04
	//
	// --------------------------------------------------------------------------
	inline int32_t GetFieldInt(int Column) const {return mpQuery->GetFieldInt(Column);}
	inline void GetFieldInt(int Column, int32_t &rIntOut) const {rIntOut = mpQuery->GetFieldInt(Column);}
	inline std::string GetFieldString(int Column) const
	{
		std::string str;
		mpQuery->GetFieldString(Column, str);
		return str;
	}
	inline void GetFieldString(int Column, std::string &rStringOut) const {mpQuery->GetFieldString(Column, rStringOut);}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    DatabaseQuery::GetSingleValue[Int|String]([reference])
	//		Purpose: When a query returns a single row containing a single column,
	//				 return that value. Will exception if row and int count aren't equal to 1.
	//				 Not const, as may call Next() to retrieve the first row.
	//		Created: 10/5/04
	//
	// --------------------------------------------------------------------------
	int32_t GetSingleValueInt();
	inline void GetSingleValueInt(int32_t &rIntOut) {rIntOut = GetSingleValueInt();}
	inline std::string GetSingleValueString()
	{
		std::string str;
		GetSingleValueString(str);
		return str;
	}
	void GetSingleValueString(std::string &rStringOut);

	// For testing
	static void TEST_VendoriseStatement(DatabaseDriver &rDriver, const char *Input, std::string &rOutput)
		{ VendoriseStatement(rDriver, Input, rOutput); }

protected:
	// Must be implemented by derived classes
	// Get the base SQL statement (not a std::string as this will mostly be a constant)
	virtual const char *GetSQLStatement() = 0;
	// Does the string need to be modified for the vendor?
	virtual bool StatementNeedsVendorisation() = 0;
	// Should the statement be prepared on the database server? (for repeated executions)
	typedef enum
	{
		NoPrepare = 0,			// one shot execution
		PrepareInstance = 1,	// prepared statement held for entire lifetime of object
		PrepareGlobal = 2		// prepared statement held for lifetime of connection
	} MultipleExecutions_t;
	virtual MultipleExecutions_t PrepareForMultipleExecutions();

	void CheckSingleValueConditions();
	
	DatabaseConnection &GetConnection() {return mConnection;}

private:
	static void VendoriseStatement(DatabaseDriver &rDriver, const char *Input, std::string &rOutput);

private:
	DatabaseConnection &mConnection;
	DatabaseDrvQuery *mpQuery;

	// Pointer to null query class
	static DatabaseDrvQuery *spNullQuery;
};


#endif // DATABASEQUERY__H
