
# perl fragment, not directly runnable

{
	# test for sqlite3
	my $sqlite3_available = do_test('Name' => 'SQLite3',
		'TestCompileFlags' => '-lsqlite3 ',
		'SuccessCompileFlags' => '-DPLATFORM_SQLITE3',
		'SuccessFlags' => ['LIBTRANS_-lsqlite=>-lsqlite3'],
		'Code' => <<__E);
#include "sqlite3.h"
int main(int argc, char *argv[])
{
        ::sqlite3_open(0,0);
        ::sqlite3_get_table(0,0,0,0,0,0);
        ::sqlite3_mprintf(0);
        ::sqlite3_free(0);
        ::sqlite3_last_insert_rowid(0);
        ::sqlite3_close(0);
        return 0;
}
__E
	unless($sqlite3_available)
	{
		# test for sqlite v2 if sqlite3 isn't present
		do_test('Name' => 'SQLite',
			'TestCompileFlags' => '-lsqlite ',
			'FailureText' => "\n*** SQLite database driver disabled\n\n",
			'FailureFlags' => ['IGNORE_lib/dbdrv_sqlite'],
			'Code' => <<__E);
#include "sqlite.h"
int main(int argc, char *argv[])
{
	::sqlite_open(0,0,0);
	::sqlite_get_table(0,0,0,0,0,0);
	::sqlite_mprintf(0);
	::sqlite_freemem(0);
	::sqlite_last_insert_rowid(0);
	::sqlite_close(0);
	return 0;
}
__E
	}

	# test for MySQL
	do_test('Name' => 'MySQL',
		'TestCompileFlags' => '-lmysqlclient ',
		'FailureText' => "\n*** MySQL database driver disabled\n\n",
		'FailureFlags' => ['IGNORE_lib/dbdrv_mysql'],
		'Code' => <<__E);
#include "mysql/mysql.h"
int main(int argc, char *argv[])
{
	::mysql_options(0,MYSQL_OPT_CONNECT_TIMEOUT,0);
	::mysql_init(0);
	::mysql_real_connect(0,0,0,0,0,0,0,0);
	::mysql_close(0);
	::mysql_real_escape_string(0,0,0,0);
	::mysql_real_query(0,0,0);
	::mysql_store_result(0);
	::mysql_affected_rows(0);
	::mysql_num_fields(0);
	::mysql_num_rows(0);
	::mysql_fetch_row(0);
	::mysql_insert_id(0);
	return 0;
}
__E

	# test for PostgreSQL
	my $pg_available = do_test('Name' => 'PostgreSQL',
		'TestCompileFlags' => '-lpq ',
		'FailureText' => "\n*** PostgreSQL database driver disabled\n\n",
		'FailureFlags' => ['IGNORE_lib/dbdrv_postgresql'],
		'Code' => <<__E);
#include "postgresql/libpq-fe.h"
int main(int argc, char *argv[])
{
	PGconn *pconnection = ::PQconnectdb("");
	bool z = (::PQstatus(pconnection) != CONNECTION_OK);
	::PQfinish(pconnection);
	size_t len = ::PQescapeString(NULL, "", 0);
	PGresult *presults = ::PQexec(pconnection, "sql");
	::PQresultStatus(presults);
	::PQclear(presults);
	return 0;
}
__E
	# if PostgreSQL is available, see if the new API stuff is there
	if($pg_available)
	{
		do_test('Name' => 'PostgreSQL7.4+',
			'TestCompileFlags' => '-lpq -lpgtypes ',
			'FailureFlags' => ['LIBTRANS_-lpgtypes=>'],
			'FailureCompileFlags' => '-DPLATFORM_POSTGRESQL_OLD_API',
			'Code' => <<__E);
#include "postgresql/libpq-fe.h"
extern "C" {
#include "postgresql/pgtypes_numeric.h"
}
int main(int argc, char *argv[])
{
	PGconn *pconnection = ::PQconnectdb("");
	PGresult *presults = ::PQexecParams(pconnection, "sql", 0, NULL, NULL, NULL, NULL, 1);
	::PQfformat(presults, 0);
	::PQftype(presults, 0);
	::PGTYPESnumeric_to_long(0,0);
	return 0;
}
__E
	}
}

1;

