// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverPostgreSQL.h
//		Purpose: Database driver for PostgreSQL
//		Created: 26/12/04
//
// --------------------------------------------------------------------------

#ifndef DBDRIVERPOSTGRESQL__H
#define DBDRIVERPOSTGRESQL__H

#include "postgresql/libpq-fe.h"

#include "DatabaseDriver.h"

class DbQueryPostgreSQL;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbDriverPostgreSQL
//		Purpose: Database driver for PostgreSQL
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
class DbDriverPostgreSQL : public DatabaseDriver
{
	friend class DbQueryPostgreSQL;
public:
	DbDriverPostgreSQL();
	virtual ~DbDriverPostgreSQL();
private:
	// no copying
	DbDriverPostgreSQL(const DbDriverPostgreSQL &);
	DbDriverPostgreSQL &operator=(const DbDriverPostgreSQL &);
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
	PGconn *GetPGconn() const {return mpConnection;}
	// For query objects to report error messages
	void ReportPostgreSQLError(PGconn *pConnection = NULL);

private:
	PGconn *mpConnection;
};

#endif // DBDRIVERPOSTGRESQL__H

