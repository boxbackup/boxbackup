// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseQueryGeneric.cpp
//		Purpose: Generic database query
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdarg.h>

#include "DatabaseQueryGeneric.h"
#include "autogen_DatabaseException.h"

#include "MemLeakFindOn.h"

// Maximum number of parameters to the variable arguments Execute() function
#define MAX_PARAMETERS		32


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::DatabaseQueryGeneric(DatabaseConnection &, const char *)
//		Purpose: Constructor, taking query and SQL statement
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DatabaseQueryGeneric::DatabaseQueryGeneric(DatabaseConnection &rConnection, const char *pStatement, bool VendoriseStatement)
	: DatabaseQuery(rConnection),
	  mpStatement(pStatement),
	  mVendoriseStatement(VendoriseStatement)
{
	ASSERT(mpStatement != 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::~DatabaseQueryGeneric()
//		Purpose: Destructor
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DatabaseQueryGeneric::~DatabaseQueryGeneric()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::Execute()
//		Purpose: Execute statement, with no parameters
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQueryGeneric::Execute()
{
	// Marshall no parameters
	DatabaseQuery::Execute(0, NULL, NULL);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::Execute(int)
//		Purpose: Execute a statement, with one integer value
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQueryGeneric::Execute(int Value)
{
	static const Database::FieldType_t paramTypes[] = {Database::Type_Int32};
	const void *params[] = {&Value};
	DatabaseQuery::Execute(1, paramTypes, params);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::Execute(const std::string &)
//		Purpose: Execute a statement, with one string value
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQueryGeneric::Execute(const std::string &rValue)
{
	static const Database::FieldType_t paramTypes[] = {Database::Type_String};
	const void *params[] = {rValue.c_str()};
	DatabaseQuery::Execute(1, paramTypes, params);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::Execute(const char *)
//		Purpose: Execute a statement, with one string value (can be NULL)
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQueryGeneric::Execute(const char *pValue)
{
	static const Database::FieldType_t paramTypes[] = {Database::Type_String};
	const void *params[] = {pValue};
	DatabaseQuery::Execute(1, paramTypes, params);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::Execute(const char *, ...)
//		Purpose: Execute a statement with multiple, typed, values.
//				 Within types, each char represents one argument. i = integer (value, cannot be NULL),
//				 I = integer (pointer, can be null), s = const char * string (pointer, can be NULL),
//				 S = std::string (pointer, can be null),
//				 N = null (corresponding argument must be NULL or 0)
//				 Not intended to be the most efficient way of doing things -- just convenient.
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseQueryGeneric::Execute(const char *Types, ...)
{
	// Quickly use no parameters version?
	if(Types == NULL || Types[0] == '\0')
	{
		DatabaseQuery::Execute(0, NULL, NULL);
		return;
	}

	// Build list of parameters
	Database::FieldType_t paramTypes[MAX_PARAMETERS];
	const void *params[MAX_PARAMETERS];
	int32_t integers[MAX_PARAMETERS];
	int numParams = 0;
	va_list args;
	va_start(args, Types);

	const char *c = Types;
	while(*c != '\0')
	{
		// Too many?
		if(numParams >= MAX_PARAMETERS)
		{
			THROW_EXCEPTION(DatabaseException, TooManyParametersToExecute)
		}
		// Set the right type
		switch(*c)
		{
		case 'i':
			{
				paramTypes[numParams] = Database::Type_Int32;
				integers[numParams] = va_arg(args, int);
				params[numParams] = &(integers[numParams]);
				break;
			}
		case 'I':
			{
				paramTypes[numParams] = Database::Type_Int32;
				params[numParams] = va_arg(args, int32_t*);
				break;
			}
		case 's':
			{
				paramTypes[numParams] = Database::Type_String;
				params[numParams] = va_arg(args, const char *);
				break;
			}
		case 'S':
			{
				paramTypes[numParams] = Database::Type_String;
				std::string *pstring = va_arg(args, std::string *);
				params[numParams] = (pstring == NULL)?NULL:(pstring->c_str());
				break;
			}
		case 'N':
			{
				paramTypes[numParams] = Database::Type_String;
				params[numParams] = NULL;
				const char *dummy = va_arg(args, const char *);
				ASSERT(dummy == NULL);
				break;
			}
		default:
			{
				THROW_EXCEPTION(DatabaseException, UnknownTypeCharacter)
			}
		}
		// Next entry
		++c;
		++numParams;
	}
	va_end(args);

	// Execute it
	DatabaseQuery::Execute(numParams, paramTypes, params);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::GetSQLStatement()
//		Purpose: Interface for implementation
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
const char *DatabaseQueryGeneric::GetSQLStatement()
{
	return mpStatement;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseQueryGeneric::StatementNeedsVendorisation()
//		Purpose: Interface for implementation
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
bool DatabaseQueryGeneric::StatementNeedsVendorisation()
{
	return mVendoriseStatement;
}


