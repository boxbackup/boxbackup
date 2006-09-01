// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverInsertParameters.cpp
//		Purpose: Utility function for drivers which can't insert parameters with the native API
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "DbDriverInsertParameters.h"
#include "DatabaseDriver.h"
#include "autogen_DatabaseException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverInsertParameters(const char *, int, const Database::FieldType_t *, const void **, const DatabaseDriver &, std::string &)
//		Purpose: Utility function for drivers which can't insert parameters with the native API
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbDriverInsertParameters(const char *SQLStatement, int NumberParameters, const Database::FieldType_t *pParameterTypes,
	const void **pParameters, const DatabaseDriver &rDriver, std::string &rStatementOut)
{
	ASSERT(SQLStatement != 0);

	// Output string
	std::string result;

	// Copy in bits of the string, and add in the parameters where marked
	int lastParameter = -1;
	int s = 0;
	int p = 0;
	char quotes = 0;
	while(SQLStatement[p] != '\0')
	{
		switch(SQLStatement[p])
		{
		case '\'':
		case '"':
			if(quotes == SQLStatement[p])
			{
				// End of quotes
				quotes = 0;
			}
			else
			{
				// Beginning of quotes
				quotes = SQLStatement[p];
			}
			break;
		
		case '$': // Insertation marker
			{
				// Store last bit of string
				if(s != p)
				{
					result.append(SQLStatement + s, p - s);
				}
				// Move to next character
				++p;

				// First character
				int num = 0;
				if(SQLStatement[p] >= '0' && SQLStatement[p] <= '9')
				{
					num = SQLStatement[p] - '0';
				}
				else
				{
					THROW_EXCEPTION(DatabaseException, BadInsertationMarker)
				}
				++p;
				
				// Is a second character present?
				if(SQLStatement[p] >= '0' && SQLStatement[p] <= '9')
				{
					num *= 10;
					num += SQLStatement[p] - '0';
					++p;
				}

				// The number specified is 1-based, change to zero based
				--num;

				// Check the parameter is in range
				if(num < 0 || num >= NumberParameters)
				{
					THROW_EXCEPTION(DatabaseException, InsertationMarkerOutOfRange)
				}

				// Check the parameter is in order
				if(num <= lastParameter)
				{
					THROW_EXCEPTION(DatabaseException, InsertationMarkersNotInOrder)
				}

				// Convert the parameter
				if(pParameters[num] == NULL)
				{
					// Null is handled differently, as is allowed for all types
					result += "NULL";
				}
				else
				{
					if(pParameterTypes[num] == Database::Type_String)
					{
						// String
						std::string quoted;
						rDriver.QuoteString((const char *)pParameters[num], quoted);
						result += quoted;
					}
					else
					{
						// Integer type -- for now don't support anything other than strings and ints
						int32_t value = 0;
						
						switch(pParameterTypes[num])
						{
						case Database::Type_Int32:
							value = *((int32_t*)(pParameters[num]));
							break;
						case Database::Type_Int16:
							value = *((int16_t*)(pParameters[num]));
							break;
					// -- removed because PostgreSQL doesn't support this type natively.
					//	case Database::Type_Int8:
					//		value = *((int8_t*)(pParameters[num]));
					//		break;
						default:
							THROW_EXCEPTION(DatabaseException, UnknownValueType)
							break;
						}
						// Add value to SQL statement
						char t[32];
						::sprintf(t, "%d", value);
						result += t;
					}
				}

				// Next bit!
				s = p;
				lastParameter = num;
				continue;
			}
			break;
		
		default:
			// A normal character, it looks lovely
			break;
		}
		// Next!
		++p;
	}
	// Add in the rest of the statement
	if(s != p)
	{
		result.append(SQLStatement + s, p - s);
	}

	// Return the result
	rStatementOut = result;
}

