// --------------------------------------------------------------------------
//
// File
//		Name:    DbQuerySqlite.h
//		Purpose: Query object for Sqlite driver
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DBQUERYSQLITE__H
#define DBQUERYSQLITE__H

#include "DatabaseDriver.h"

class DbDriverSqlite;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbQuerySqlite
//		Purpose: Query object for Sqlite driver
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
class DbQuerySqlite : public DatabaseDrvQuery
{
public:
	DbQuerySqlite(DbDriverSqlite &rDriver);
	~DbQuerySqlite();
private:
	// no copying
	DbQuerySqlite(const DbQuerySqlite &);
	DbQuerySqlite &operator=(const DbQuerySqlite &);
public:

	virtual void Execute(const char *SQLStatement, int NumberParameters,
		const Database::FieldType_t *pParameterTypes, const void **pParameters);

	virtual int GetNumberChanges() const;
	virtual int GetNumberRows() const;
	virtual int GetNumberColumns() const;

	virtual bool Next();
	virtual bool HaveRow() const;

	virtual void Finish();

	virtual bool IsFieldNull(int Column) const;

	virtual int32_t GetFieldInt(int Column) const;
	virtual void GetFieldString(int Column, std::string &rStringOut) const;

private:
	DbDriverSqlite &mrDriver;
	int mNumberRows;
	int mNumberColumns;
	char **mppResults;
	int mCurrentRow;
	int mNumberChanges;
};

#endif // DBQUERYSQLITE__H

