// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverInsertParameters.cpp
//		Purpose: Utility function for drivers which can't insert parameters with the native API
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DBDRIVERINSERTPARAMETERS__H
#define DBDRIVERINSERTPARAMETERS__H

#include <string>
#include "DatabaseTypes.h"
class DatabaseDriver;

void DbDriverInsertParameters(const char *SQLStatement, int NumberParameters, const Database::FieldType_t *pParameterTypes,
	const void **pParameters, const DatabaseDriver &rDriver, std::string &rStatementOut);

#endif // DBDRIVERINSERTPARAMETERS__H

