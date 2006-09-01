// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverSqlite.h
//		Purpose: Database driver for Sqllite
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DBDRIVERSQLITE__H
#define DBDRIVERSQLITE__H

#ifdef PLATFORM_SQLITE3
	#include "sqlite3.h"
#else
	#include "sqlite.h"
#endif

#include "DatabaseDriver.h"

class DbQuerySqlite;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbDriverSqlite
//		Purpose: Database driver for Sqllite
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
class DbDriverSqlite : public DatabaseDriver
{
	friend class DbQuerySqlite;
public:
	DbDriverSqlite();
	virtual ~DbDriverSqlite();
private:
	// no copying
	DbDriverSqlite(const DbDriverSqlite &);
	DbDriverSqlite &operator=(const DbDriverSqlite &);
public:

	virtual const char *GetDriverName() const;

	virtual void Connect(const std::string &rConnectionString, int Timeout);
	virtual DatabaseDrvQuery *Query();
	virtual void Disconnect();

	virtual int32_t GetLastAutoIncrementValue(const char *TableName, const char *ColumnName);

	virtual void QuoteString(const char *pString, std::string &rStringQuotedOut) const;

	virtual const TranslateMap_t &GetGenericTranslations();

protected:
	// For query objects to get the open database file
#ifdef PLATFORM_SQLITE3
	sqlite3 *
#else
	sqlite *
#endif
		GetSqlite() const {return mpConnection;}

private:
#ifdef PLATFORM_SQLITE3
	sqlite3 *mpConnection;
#else
	sqlite *mpConnection;
#endif
};

#endif // DBDRIVERSQLITE__H

