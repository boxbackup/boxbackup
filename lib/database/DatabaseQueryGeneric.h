// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseQueryGeneric.h
//		Purpose: Generic database query
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASEQUERYGENERIC__H
#define DATABASEQUERYGENERIC__H

#include "DatabaseQuery.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    DatabaseQueryGeneric
//		Purpose: 
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
class DatabaseQueryGeneric : public DatabaseQuery
{
public:
	DatabaseQueryGeneric(DatabaseConnection &rConnection, const char *pStatement, bool VendoriseStatement = false);
	~DatabaseQueryGeneric();
private:
	// no copying
	DatabaseQueryGeneric(const DatabaseQueryGeneric &);
	DatabaseQueryGeneric &operator=(const DatabaseQueryGeneric &);
public:

	// Execution of the query
	void Execute();
	void Execute(int Value);
	void Execute(const std::string &rValue);
	void Execute(const char *pValue);
	void Execute(const char *Types, ...);

protected:
	// Implementation details
	virtual const char *GetSQLStatement();
	virtual bool StatementNeedsVendorisation();

private:
	const char *mpStatement;
	bool mVendoriseStatement;
};

#endif // DATABASEQUERYGENERIC__H

