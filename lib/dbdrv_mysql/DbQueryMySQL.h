// --------------------------------------------------------------------------
//
// File
//		Name:    DbQueryMySQL.h
//		Purpose: Query object for MySQL driver
//		Created: 11/5/04
//
// --------------------------------------------------------------------------

#ifndef DBQUERYMYSQL__H
#define DBQUERYMYSQL__H

#include "mysql/mysql.h"

#include "DatabaseDriver.h"

class DbDriverMySQL;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DbQueryMySQL
//		Purpose: Query object for MySQL driver
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
class DbQueryMySQL : public DatabaseDrvQuery
{
public:
	DbQueryMySQL(DbDriverMySQL &rDriver);
	~DbQueryMySQL();
private:
	// no copying
	DbQueryMySQL(const DbQueryMySQL &);
	DbQueryMySQL &operator=(const DbQueryMySQL &);
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

protected:
	inline bool HaveResults() const
	{
		return mQueryReturnedData?(mpResults != 0):(mChangedRows >= 0);
	}

private:
	DbDriverMySQL &mrDriver;
	MYSQL_RES *mpResults;
	bool mQueryReturnedData;
	int64_t mChangedRows;
	int mNumberRows;
	int mNumberColumns;
	bool mFetchedFirstRow;
	MYSQL_ROW mCurrentRow;
	unsigned long *mCurrentRowFieldLengths;
};

#endif // DBQUERYMYSQL__H

