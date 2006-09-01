// --------------------------------------------------------------------------
//
// File
//		Name:    testdatabase.cpp
//		Purpose: Test database driver library
//		Created: 10/5/04
//
// --------------------------------------------------------------------------


#include "Box.h"

#include <string.h>
#include <map>
#include <string>

#include "Test.h"
// include this next one first, to check required headers are built in properly
#include "autogen_db/testqueries.h"
#include "DatabaseConnection.h"
#include "DatabaseQueryGeneric.h"
#include "DatabaseDriverRegistration.h"
#include "autogen_db/testdb_schema.h"
#include "autogen_db/testdatabase_query.h"
#include "autogen_DatabaseException.h"

#ifdef MODULE_lib_dbdrv_postgresql
	// see notes in DbQueryPostgreSQL.h
	#include "PostgreSQLOidTypes.h"
#endif


#include "MemLeakFindOn.h"

char *insertTestString[] = {"a string", "pants'", "\"hello\"", "lampshade", 0};
int insertTestInteger[] = {324234, 322, 4, 1};
#define NUMBER_VALUES 4

void test_zero_changes_from_select(const char *driver, DatabaseQuery &rQuery)
{
	// Work around a bug in sqlite...
#ifdef PLATFORM_SQLITE3
	if(::strcmp(driver, "sqlite") != 0)
#endif
	{
		TEST_THAT(rQuery.GetNumberChanges() == 0);
	}
}


void check_pg_oid_types(DatabaseConnection &rdb)
{
#ifdef MODULE_lib_dbdrv_postgresql
	/*
		(note that there is extra whitespace after the introductory and end lines)
		SQL Query
		[
			Name: CheckOID
			Statement: select oid,typname from pg_type;
			Results: int OID, std::string Name
		]
	*/
	CheckOID query(rdb);
	query.Execute();
	std::map<std::string,int> o;
	while(query.Next())
	{
		o[query.GetName()] = query.GetOID();
	}
	bool all_postgresql_oid_types_correct = true;
	#define CHECK_OID_VAL(name,value) {std::map<std::string,int>::iterator i(o.find(name)); if(i == o.end() || i->second != value) {all_postgresql_oid_types_correct = false;}}
	CHECK_OID_VAL("int4", INT4OID);
	CHECK_OID_VAL("int2", INT2OID);
	CHECK_OID_VAL("text", TEXTOID);
	CHECK_OID_VAL("name", NAMEOID);
	CHECK_OID_VAL("varchar", VARCHAROID);
	CHECK_OID_VAL("bpchar", BPCHAROID);
	CHECK_OID_VAL("bool", BOOLOID);
	CHECK_OID_VAL("char", CHAROID);
	CHECK_OID_VAL("oid", OIDOID);
	CHECK_OID_VAL("int8", INT8OID);
	//CHECK_OID_VAL("", );
	TEST_THAT(all_postgresql_oid_types_correct);
	if(all_postgresql_oid_types_correct)
	{
		::printf("PostgreSQL OID types correct.\n");
	}
#endif
}


void test_database(const char *driver, const char *connectionstring)
{
	// Is the driver available?
	if(!Database::DriverAvailable(driver))
	{
		::printf("Driver %s not available, skipping tests for that database\n", driver);
		return;
	}
	::printf("Testing interface with driver %s...\n", driver);

	DatabaseConnection db;
	try
	{
		db.Connect(std::string(driver), std::string(connectionstring), 1000 /* timeout */);
	}
	catch(DatabaseException &e)
	{
		if(e.GetSubType() != DatabaseException::FailedToConnect) throw;
		::printf("Failed to connect to database server with driver %s, skipping rest of test\n", driver);
		bool failedToConnect = false;
		TEST_THAT(failedToConnect);
		return;
	}

	// Check name
	TEST_THAT(::strcmp(driver, db.GetDriverName()) == 0);

	// If postgresql, check OID ids
	if(::strcmp(driver, "postgresql") == 0)
	{
		check_pg_oid_types(db);
	}
	
	// Test string quoting
	{
		std::string quoted;
		db.QuoteString("quotes", quoted);
		TEST_THAT(quoted == "'quotes'");
	}

	// Create basic schema, using autogen function
	testdb_Create(db);
	// Insert some values
	{
		int last_insertid = -1;
		DatabaseQueryGeneric insert(db, "INSERT INTO tTest1(fInteger,fString) VALUES($1,$2)");
		for(int n = 0; n < NUMBER_VALUES; ++n)
		{
			insert.Execute("is", insertTestInteger[n], insertTestString[n]);
			int iid = db.GetLastAutoIncrementValue("tTest1", "fID");
			TEST_THAT(iid > last_insertid);
			last_insertid = iid;
		}
	}
	// Read them out in sorted order
	{
		DatabaseQueryGeneric read(db, "SELECT fInteger,fString FROM tTest1 ORDER BY fInteger");
		read.Execute();
		test_zero_changes_from_select(driver, read);
		TEST_THAT(read.GetNumberRows() == NUMBER_VALUES);
		TEST_THAT(read.GetNumberColumns() == 2);
		int n = NUMBER_VALUES - 1;
		while(read.Next())
		{
			//::printf("|%d|%s|\n", read.GetFieldInt(0), read.GetFieldString(1).c_str());
			TEST_THAT(n >= 0);
			TEST_THAT(read.GetFieldInt(0) == insertTestInteger[n]);
			TEST_THAT(read.GetFieldString(1) == insertTestString[n]);
			--n;
		}
		TEST_THAT(n == -1);
	}
	// Check single values work as expected
	{
		DatabaseQueryGeneric count(db, "SELECT COUNT(*) FROM tTest1");
		count.Execute();
		TEST_THAT(count.GetSingleValueInt() == NUMBER_VALUES);
		// Check it works again (will have modified the data the first time around)
		TEST_THAT(count.GetSingleValueInt() == NUMBER_VALUES);
	}
	{
		DatabaseQueryGeneric count(db, "SELECT fString FROM tTest1 WHERE fInteger=4");
		count.Execute();
		TEST_THAT(count.GetSingleValueString() == "\"hello\"");
		// Check it works again (will have modified the data the first time around)
		TEST_THAT(count.GetSingleValueString() == "\"hello\"");
	}
	// Check update row counts are OK
	{
		DatabaseQueryGeneric update(db, "UPDATE tTest1 SET fInteger=(fInteger+1) WHERE fInteger>400");
		update.Execute();
		TEST_THAT(update.GetNumberChanges() == 1);
		TEST_THAT(update.Next() == false);
	}
	// Try an autogenerated query
	{
	/*
		(note that there is extra whitespace after the introductory and end lines)
		SQL Query	
		[	
			Name: Test1
			Statement: SELECT fInteger,fString FROM tTest1 WHERE fInteger>$1
			Parameters: int
			Results: int Integer, std::string String
		]	
	*/
		Test1 query(db);
		query.Execute(3);
		int n = 0;
		while(query.Next())
		{
			++n;
		}
		TEST_THAT(n == 3);
		test_zero_changes_from_select(driver, query);
	}
	// And one which was autogenerated in another file
	{
		Test2 query(db);
		std::string string("lampshade");
		query.Execute(1, &string);
		TEST_THAT(query.Next());
		TEST_THAT(query.GetInteger() == 1);
		TEST_THAT(query.GetString() == "lampshade");
		TEST_THAT(!query.Next());
		test_zero_changes_from_select(driver, query);
	}
	// And another which returns a single value
	{
		TEST_THAT(Test3::Do(db, 1) == "lampshade");
	}
	// A query, autogenerated, which takes the statement at runtime
	{
		TestRuntimeQuery query(db, "SELECT fInteger,fString FROM tTest1 WHERE fInteger=$1");
		query.Execute("i", 1);
		TEST_THAT(query.Next());
		TEST_THAT(query.GetInteger() == 1);
		TEST_THAT(query.GetString() == "lampshade");
		TEST_THAT(!query.Next());
		test_zero_changes_from_select(driver, query);
	}
	// And finally, a query which returns an insert value
	{
	/*
		SQL Query
		[
			Name: Test4
			Statement: INSERT INTO tTest1(fInteger,fString) VALUES($1,$2)
			Parameters: int, std::string
			AutoIncrementValue: tTest1 fID
		]
	*/
		int id = Test4::Do(db, 56, "xx1");
		int id2 = Test4::Do(db, 898, "pdfdd");
		TEST_THAT(id2 > id);
		{
			Test4 query(db);
			query.Execute(2938, "ajjd");
			int id3 = query.InsertedValue();
			TEST_THAT(id3 > id2);
		}
	}
	// Drop the schema, using autogen function
	testdb_Drop(db);
}

// Vendorisation test driver
class VendorTest : public DatabaseDriver
{
public:
	VendorTest() {}
	~VendorTest() {}
	virtual const char *GetDriverName() const {return "vendortest";}
	virtual void Connect(const std::string &rConnectionString, int Timeout) {}
	virtual DatabaseDrvQuery *Query() {return 0;}
	virtual void Disconnect() {};
	virtual void QuoteString(const char *pString, std::string &rStringQuotedOut) const {}
	virtual int32_t GetLastAutoIncrementValue(const char *TableName, const char *ColumnName) {return 0;}
	virtual const TranslateMap_t &GetGenericTranslations()
	{
		static DatabaseDriver::TranslateMap_t table;
		const char *from[] = {"TEST", "ARGS2", "X1", "Z1", 0};
		const char *to[] = {"OUTPUT", "GG[!0:!1:!0", "Y !01", "!0U", 0};
		DATABASE_DRIVER_FILL_TRANSLATION_TABLE(table, from, to);
		return table;
	}
};

void test_vendorisation()
{
	VendorTest driver;
	std::string o;
	bool printres = false;
	#define TEST_TRANS(from, shouldbe)								\
		DatabaseQuery::TEST_VendoriseStatement(driver, from, o);	\
		if(printres) {::printf("|%s|->|%s|\n", from, o.c_str()); }	\
		TEST_THAT(o == shouldbe);
	TEST_TRANS("0123TEST4567", "0123TEST4567");
	TEST_TRANS("0123`TEST4567", "0123OUTPUT4567");
	TEST_TRANS("0123`TEST", "0123OUTPUT");
	TEST_TRANS("0123`X(ywy2yyy2)SS", "0123Y ywy2yyy21SS");
	TEST_TRANS("0123`X(ywy2yyy2)", "0123Y ywy2yyy21");
	TEST_TRANS("`X(ywy2yyy2)SS", "Y ywy2yyy21SS");
	TEST_TRANS("`Z(sjs8u)SS", "sjs8uUSS");
	TEST_TRANS("0123`ARGS(hy2, x23s)4567", "0123GG[hy2: x23s:hy24567");
	TEST_TRANS("0123`ARGS(hy2, x23s)", "0123GG[hy2: x23s:hy2");
	TEST_TRANS("0123`ARGS(, x23s)", "0123GG[: x23s:");
	TEST_TRANS("0123`ARGS(hy2,)", "0123GG[hy2::hy2");
}

int test(int argc, const char *argv[])
{
	// Test vendorisation
	test_vendorisation();

	// How many drivers?
	const char *driverList = 0;
	int nDrivers = Database::DriverList(&driverList);
	TEST_THAT(driverList != 0);
	::printf("%d drivers available: %s\n", nDrivers, driverList);

	// Test the same code on all databases
	test_database("sqlite", "testfiles/testdb.sqlite");
	test_database("mysql", "testdb:testuser:password");
	test_database("postgresql", "dbname = test");

	return 0;
}

