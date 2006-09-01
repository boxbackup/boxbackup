// --------------------------------------------------------------------------
//
// File
//		Name:    DbQueryPostgreSQL.h
//		Purpose: Query object for PostgreSQL driver
//		Created: 26/12/04
//
// --------------------------------------------------------------------------

#ifndef DBQUERYPOSTGRESQL__H
#define DBQUERYPOSTGRESQL__H

#include "postgresql/libpq-fe.h"

#include "DatabaseDriver.h"

class DbDriverPostgreSQL;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbQueryPostgreSQL
//		Purpose: Query object for PostgreSQL driver
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
class DbQueryPostgreSQL : public DatabaseDrvQuery
{
public:
	DbQueryPostgreSQL(DbDriverPostgreSQL &rDriver);
	~DbQueryPostgreSQL();
private:
	// no copying
	DbQueryPostgreSQL(const DbQueryPostgreSQL &);
	DbQueryPostgreSQL &operator=(const DbQueryPostgreSQL &);
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
	DbDriverPostgreSQL &mrDriver;
	PGresult *mpResults;
	bool mQueryReturnedData;
	int64_t mChangedRows;
	int mNumberRows;
	int mNumberColumns;
	int mCurrentRow;
};

#endif // DBQUERYPOSTGRESQL__H

