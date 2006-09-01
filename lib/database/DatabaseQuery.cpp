// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseQuery.cpp
//		Purpose: Database query abstraction
//		Created: 1/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "DatabaseQuery.h"
#include "DatabaseConnection.h"
#include "autogen_DatabaseException.h"
#include "DatabaseDriver.h"

#include "MemLeakFindOn.h"

#define MAX_GENERIC_ARGUMENTS		10

// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::DatabaseQuery()
//		Purpose: Constructor
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
DatabaseQuery::DatabaseQuery(DatabaseConnection &rConnection)
	: mConnection(rConnection),
	  mpQuery(spNullQuery)
{
	ASSERT(mpQuery != 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::~DatabaseQuery()
//		Purpose: Destructor
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
DatabaseQuery::~DatabaseQuery()
{
	if(mpQuery != spNullQuery)
	{
		Finish();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::Execute(int, const FieldType_t *, void **)
//		Purpose: Execute a query
//		Created: 7/5/04
//
// --------------------------------------------------------------------------
void DatabaseQuery::Execute(int NumberParameters, const Database::FieldType_t *pParameterTypes, const void **pParameters)
{
	// Check parameters
	if(NumberParameters < 0 || (NumberParameters > 0 && (pParameterTypes == 0 || pParameters == 0)))
	{
		THROW_EXCEPTION(DatabaseException, BadQueryParameters)
	}
	if(NumberParameters > Database::MaxParameters)
	{
		THROW_EXCEPTION(DatabaseException, TooManyParameters)
	}

	// Need a query object?
	if(mpQuery == spNullQuery)
	{
		// No object currently held -- create a new object from the driver
		DatabaseDriver &rdriver(mConnection.GetDriver());
		mpQuery = rdriver.Query();
		// Paranoid check
		if(mpQuery == NULL)
		{
			mpQuery = spNullQuery;
		}
	}

	// Execute the query on the driver's query object
	if(StatementNeedsVendorisation())
	{
		// Statement needs running through the vendorisation routine
		DatabaseDriver &rdriver(mConnection.GetDriver());
		std::string statement;
		VendoriseStatement(rdriver, GetSQLStatement(), statement);
		mpQuery->Execute(statement.c_str(), NumberParameters, pParameterTypes, pParameters);
	}
	else
	{
		// Plain statement can be used
		mpQuery->Execute(GetSQLStatement(), NumberParameters, pParameterTypes, pParameters);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::VendoriseStatement(const char *, std::string &)
//		Purpose: Replace generic representations in an SQL string with vendor
//				 specific SQL.
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
void DatabaseQuery::VendoriseStatement(DatabaseDriver &rDriver, const char *Input, std::string &rOutput)
{
	ASSERT(Input != NULL);
	rOutput = std::string(); // ie clear();
	
	// Scan for ` characters
	int b = 0;
	int p = 0;
	while(Input[p] != '\0')
	{
		if(Input[p] == '`')
		{
			// Start of generic representation
			// Dump previous string to output
			if(b != p)
			{
				rOutput.append(Input + b, p - b);
			}

			// Details of element
			const char *element[MAX_GENERIC_ARGUMENTS];
			int elementLength[MAX_GENERIC_ARGUMENTS];
			int numberElements = 1;
			
			// First element, which is the name of the string
			++p;
			b = p;
			element[0] = Input + p;
			while(Input[p] != '\0' && ((Input[p] >= 'A' && Input[p] <= 'Z') || Input[p] == '_'))
			{
				++p;
			}
			elementLength[0] = p - b;
			
			// Any arguments?
			if(Input[p] == '(')
			{
				// Yes!
				++p;
				while(Input[p] != ')')
				{
					if(numberElements >= MAX_GENERIC_ARGUMENTS)
					{
						THROW_EXCEPTION(DatabaseException, TooManyArgumentsToGeneric)
					}
					element[numberElements] = Input + p;
					int eb = p;
					while(Input[p] != ',' && Input[p] != ')')
					{
						++p;
					}
					elementLength[numberElements] = p - eb;
					++numberElements;
					if(Input[p] != ')')
					{
						// Move on
						++p;
					}
				}
				// Make sure final zero length args work OK
				if(Input[p] == ')' && Input[p-1] == ',')
				{
					if(numberElements >= MAX_GENERIC_ARGUMENTS)
					{
						THROW_EXCEPTION(DatabaseException, TooManyArgumentsToGeneric)
					}
					element[numberElements] = Input + p;
					elementLength[numberElements] = 0;
					++numberElements;
				}
				// Move on to the next character, skipping the final ')'
				++p;
			}
			
			// Translate it and add on to the output
			std::string vendor;
			rDriver.TranslateGeneric(numberElements, element, elementLength, vendor);
			rOutput += vendor;
			
			// Ready for next untranslated section
			b = p;
		}
		else
		{
			// Examine next character
			++p;
		}
	}
	// Add the end of the string
	if(b != p)
	{
		rOutput.append(Input + b, p - b);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::Finish()
//		Purpose: Explicitly deallocate the query resources. Can leave it up to the
//				 destructor in most circumstances.
//		Created: 8/5/04
//
// --------------------------------------------------------------------------
void DatabaseQuery::Finish()
{
	// Check that a query is in progress
	if(mpQuery == spNullQuery)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}

	// Tell query to finish
	mpQuery->Finish();

	// Clean up resource
	delete mpQuery;
	mpQuery = spNullQuery;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::GetSingleValueInt()
//		Purpose: Return a single value
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int32_t DatabaseQuery::GetSingleValueInt()
{
	CheckSingleValueConditions();
	return mpQuery->GetFieldInt(0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::GetSingleValueString(std::string &)
//		Purpose: Return a single value
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQuery::GetSingleValueString(std::string &rStringOut)
{
	CheckSingleValueConditions();
	mpQuery->GetFieldString(0, rStringOut);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::CheckSingleValueConditions()
//		Purpose: Protected. Check the conditions are right to retrieve a single value.
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQuery::CheckSingleValueConditions()
{
	if(mpQuery == spNullQuery)
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	if(mpQuery->GetNumberColumns() != 1 || mpQuery->GetNumberRows() != 1)
	{
		THROW_EXCEPTION(DatabaseException, QueryContainedMoreThanSingleValue)
	}
	// Need to get the query ready?
	if(!HaveRow())
	{
		// No data ready yet, get that first row
		Next();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQuery::PrepareForMultipleExecutions()
//		Purpose: Should this queries be prepared for more efficient multiple
//				 executions?
//		Created: 8/5/04
//
// --------------------------------------------------------------------------
DatabaseQuery::MultipleExecutions_t DatabaseQuery::PrepareForMultipleExecutions()
{
	return NoPrepare;
}




// ===============================================================================================================

// Hide from the outside world
namespace
{

// A null query class which will just exception on any call
class NullQuery : public DatabaseDrvQuery
{
public:
	NullQuery()
		: DatabaseDrvQuery()
	{
	}
	~NullQuery()
	{
	}
	void Execute(const char *SQLStatement, int NumberParameters, const Database::FieldType_t *pParameterTypes, const void **pParameters)
	{
		THROW_EXCEPTION(DatabaseException, Internal)
	}
	int GetNumberChanges() const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	int GetNumberRows() const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	int GetNumberColumns() const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	bool Next()
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	bool HaveRow() const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	void Finish()
	{
		// Should never be called -- check before Finish() is called in driver query
		THROW_EXCEPTION(DatabaseException, Internal)
	}
	bool IsFieldNull(int Column) const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	int32_t GetFieldInt(int Column) const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
	void GetFieldString(int Column, std::string &rStringOut) const
	{
		THROW_EXCEPTION(DatabaseException, QueryNotExecuted)
	}
};

// Set up null query static member var
NullQuery nullQuery;
DatabaseDrvQuery *DatabaseQuery::spNullQuery = &nullQuery;

} // end hiding namespace

