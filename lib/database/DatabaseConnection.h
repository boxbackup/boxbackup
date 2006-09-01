// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseConnection.h
//		Purpose: Database connection
//		Created: 1/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASECONNECTION__H
#define DATABASECONNECTION__H

#include <string>

class DatabaseDriver;
class DatabaseQuery;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DatabaseConnection
//		Purpose: Database connection abstraction
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
class DatabaseConnection
{
	friend class DatabaseQuery;
public:
	DatabaseConnection();
	~DatabaseConnection();
private:
	// No copying
	DatabaseConnection(const DatabaseConnection &);
	DatabaseConnection &operator=(const DatabaseConnection&);

public:
	void Connect(const std::string &rDriverName, const std::string &rConnectionString, int Timeout);
	void Disconnect();

	int32_t GetLastAutoIncrementValue(const char *TableName, const char *ColumnName);

	const char *GetDriverName() const;

	// Expose the string quoting function, so that callers can use it to dynamically build queries
	void QuoteString(const char *pString, std::string &rStringQuotedOut) const;

protected:
	DatabaseDriver &GetDriver() const;

private:
	DatabaseDriver *mpDriver;
};

#endif // DATABASECONNECTION__H
