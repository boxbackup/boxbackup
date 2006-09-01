// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseDriver.h
//		Purpose: Database driver interface
//		Created: 2/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASEDRIVER__H
#define DATABASEDRIVER__H

#include <string>
#include <map>

#include "DatabaseTypes.h"

class DatabaseDrvQuery;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DatabaseDriver
//		Purpose: Database driver interface
//		Created: 2/5/04
//
// --------------------------------------------------------------------------
class DatabaseDriver
{
public:
	DatabaseDriver();
	virtual ~DatabaseDriver();
private:
	// no copying
	DatabaseDriver(const DatabaseDriver &);
	DatabaseDriver &operator=(const DatabaseDriver &);
public:

	virtual const char *GetDriverName() const = 0;

	virtual void Connect(const std::string &rConnectionString, int Timeout) = 0;
	virtual DatabaseDrvQuery *Query() = 0;
	virtual void Disconnect() = 0;

	// Info
	virtual int32_t GetLastAutoIncrementValue(const char *TableName, const char *ColumnName) = 0;

	// Utility
	virtual void QuoteString(const char *pString, std::string &rStringQuotedOut) const = 0;
	
	// Vendorisation
	virtual void TranslateGeneric(int NumberElements, const char **Element, int *ElementLength, std::string &rOut);
	typedef std::map<std::string, std::string> TranslateMap_t;
	virtual const TranslateMap_t &GetGenericTranslations();

	// Error reporting. Not overrideable.
	void ReportErrorMessage(const char *ErrorMessage);
};


// --------------------------------------------------------------------------
//
// Class
//		Name:    DatabaseDrvQuery
//		Purpose: Driver interface to query
//		Created: 7/5/04
//
// --------------------------------------------------------------------------
class DatabaseDrvQuery
{
public:
	DatabaseDrvQuery();
	virtual ~DatabaseDrvQuery();
private:
	// no copying
	DatabaseDrvQuery(const DatabaseDrvQuery &);
	DatabaseDrvQuery &operator=(const DatabaseDrvQuery &);
public:

	virtual void Execute(const char *SQLStatement, int NumberParameters,
		const Database::FieldType_t *pParameterTypes, const void **pParameters) = 0;

	virtual int GetNumberChanges() const = 0;
	virtual int GetNumberRows() const = 0;
	virtual int GetNumberColumns() const = 0;

	virtual bool Next() = 0;
	virtual bool HaveRow() const = 0;

	virtual void Finish() = 0;

	virtual bool IsFieldNull(int Column) const = 0;

	virtual int32_t GetFieldInt(int Column) const = 0;
	virtual void GetFieldString(int Column, std::string &rStringOut) const = 0;
};


#define DATABASE_DRIVER_FILL_TRANSLATION_TABLE(table, fromI, toI)					\
	const char **fromS = fromI; const char **toS = toI;								\
	if(table.size() == 0) {while(*fromS != 0) {table[std::string(*(fromS++))] = std::string(*(toS++));}}

#endif // DATABASEDRIVER__H

