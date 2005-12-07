dnl @synopsis AX_CHECK_BDB_V1
dnl
dnl This macro find an installation of Berkeley DB version 1, or compatible.
dnl It will define the following macros on success:
dnl
dnl HAVE_DB     - If Berkeley DB version 1 or compatible is available
dnl DB_HEADER   - The relative path and filename of the header file
dnl LIBS        - Updated for correct library
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/12
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_BDB_V1], [
  ac_have_bdb=no
  AC_CHECK_HEADERS([db_185.h db4/db_185.h db3/db_185.h db1/db.h db.h],
                   [ac_bdb_header=$ac_header; break], [ac_bdb_header=""])
  if test "x$ac_bdb_header" != x; then
    AC_SEARCH_LIBS([__db185_open],
                   [db db-4.3 db-4.2 db-4.1 db-4.0 db-3],
                   [ac_have_bdb=yes],
                   [AC_SEARCH_LIBS([dbopen], [db-1 db], [ac_have_bdb=yes])])
  fi
  if test "x$ac_have_bdb" = "xyes"; then
    AC_MSG_CHECKING([whether found db libraries work])
    AC_RUN_IFELSE([AC_LANG_PROGRAM([[
        $ac_includes_default
        #include "$ac_bdb_header"]], [[
        DB *dbp = dbopen(0, 0, 0, DB_HASH, 0);
        if(dbp) dbp->close(dbp);
        return 0;
      ]])], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_DB], 1, [Define to 1 if Berkeley DB is available])
      AC_DEFINE_UNQUOTED([DB_HEADER], ["$ac_bdb_header"],
                          [Define to the location of the Berkeley DB 1.85 header])
      ], [
      AC_MSG_RESULT([no])
      ac_have_bdb=no
      ])
  fi
  ])dnl
