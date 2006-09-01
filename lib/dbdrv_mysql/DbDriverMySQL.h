// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverMySQL.h
//		Purpose: Database driver for MySQL
//		Created: 11/5/04
//
// --------------------------------------------------------------------------

#ifndef DBDRIVERMYSQL__H
#define DBDRIVERMYSQL__H

#include "mysql/mysql.h"

#include "DatabaseDriver.h"

class DbQueryMySQL;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbDriverMySQL
//		Purpose: Database driver for MySQL
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
class DbDriverMySQL : public DatabaseDriver
{
	friend class DbQueryMySQL;
public:
	DbDriverMySQL();
	virtual ~DbDriverMySQL();
private:
	// no copying
	DbDriverMySQL(const DbDriverMySQL &);
	DbDriverMySQL &operator=(const DbDriverMySQL &);
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
	MYSQL *GetMYSQL() const {return mpConnection;}
	// For query objects to report error messages
	void ReportMySQLError(MYSQL *pConnection = NULL);

private:
	MYSQL *mpConnection;
};

#endif // DBDRIVERMYSQL__H

