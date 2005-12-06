#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([Box Backup], 0.09, [box@fluffy.co.uk])
AC_CONFIG_SRCDIR([lib/common/Box.h])
AC_CONFIG_HEADERS([lib/common/config.h])


### Checks for programs.

AC_LANG([C++])
AC_PROG_CXX
AC_CXX_EXCEPTIONS
AC_CXX_NAMESPACES
if test "x$ac_cv_cxx_exceptions" != "xyes" || \
   test "x$ac_cv_cxx_namespaces" != "xyes"; then
  AC_MSG_ERROR([[basic compile checks failed, the C++ compiler is broken]])
fi
if test "x$GXX" = "xyes"; then
  # Use -Wall if we have gcc. This gives better warnings
  AC_SUBST([CXXFLAGS_STRICT], ['-Wall'])
  # Use -rdynamic if we have gcc. This is needed for backtrace
  AC_SUBST([LDADD_RDYNAMIC], ['-rdynamic'])
fi


### Checks for libraries.

AC_SEARCH_LIBS([nanosleep], [rt], [ac_have_nanosleep=yes],   [])
AC_SEARCH_LIBS_PROTO([Sleep], 
	[void __stdcall Sleep(long int);], 
	[Sleep(0);],
	[kernel32],
	[ac_have_win32_sleep=yes], 
	[])
if test "x$ac_have_nanosleep"   != "xyes" && \
   test "x$ac_have_win32_sleep" != "xyes"; then
  AC_MSG_ERROR([[Cannot find a short sleep function (nanosleep or Sleep)]])
fi

AC_CHECK_LIB([z], [zlibVersion],, [AC_MSG_ERROR([[Cannot find zlib]])])
AX_CHECK_BDB_V1
VL_LIB_READLINE
have_libreadline=no
test "x$vl_cv_lib_readline" != "xno" && have_libreadline=yes


## Check for Open SSL, use old versions only if explicitly requested
AX_CHECK_SSL(, [AC_MSG_ERROR([[OpenSSL is not installed but is required]])])
AC_ARG_ENABLE(
  [old-ssl],
  [AC_HELP_STRING([--enable-old-ssl],
                  [Allow use of pre-0.9.7 Open SSL - NOT RECOMMENDED, read the documentation])])
AC_CHECK_LIB(
  [crypto],
  [EVP_CipherInit_ex],, [
  if test "x$enable_old_ssl" = "xyes"; then
    AC_DEFINE([HAVE_OLD_SSL], 1, [Define to 1 if SSL is pre-0.9.7])
  else
    AC_MSG_ERROR([[found an old (pre 0.9.7) version of SSL.
Upgrade or read the documentation for alternatives]])
  fi
  ])


### Checks for header files.

AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([execinfo.h netinet/in.h regex.h sys/types.h sys/xattr.h])


### Checks for typedefs, structures, and compiler characteristics.

AC_CHECK_TYPES([u_int8_t, u_int16_t, u_int32_t, u_int64_t])
AC_CHECK_TYPES([uint8_t, uint16_t, uint32_t, uint64_t])
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_BIGENDIAN
AX_CHECK_NONALIGNED_ACCESS
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_flags])
AC_CHECK_MEMBERS([struct stat.st_mtimespec])
AC_CHECK_MEMBERS([struct sockaddr_in.sin_len],,, [[
  #include <sys/types.h>
  #include <netinet/in.h>
  ]])
AC_CHECK_DECLS([INFTIM],,, [[#include <poll.h>]])
AC_CHECK_DECLS([SO_PEERCRED],,, [[#include <sys/socket.h>]])
AC_HEADER_TIME
AC_STRUCT_TM
AX_CHECK_DIRENT_D_TYPE
AC_SYS_LARGEFILE
AX_CHECK_LLONG_MINMAX
AX_CHECK_DEFINE_PRAGMA
AX_RANDOM_DEVICE
AX_CHECK_MOUNT_POINT([],[])
AX_CHECK_MALLOC_WORKAROUND


### Checks for library functions.

AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([getpeereid kqueue lchown setproctitle])
AX_FUNC_SYSCALL
AX_CHECK_SYSCALL_LSEEK


### Miscellaneous complicated feature checks

## Check for large file support active. AC_SYS_LARGEFILE has already worked
## out how to enable it if necessary, we need to know if we've got it so we
## can disable the raidfile intercepts
AC_CACHE_CHECK([if we have large file support enabled], [have_large_file_support],
  [AC_RUN_IFELSE([AC_LANG_PROGRAM([[$ac_includes_default]], [[
      return sizeof(off_t)==4;
    ]])],
    [have_large_file_support=yes], [have_large_file_support=no]
  )])
if test "x$have_large_file_support" = "xyes"; then
  AC_DEFINE([HAVE_LARGE_FILE_SUPPORT], 1, [Define to 1 large file support is in use])
fi

## Find out how to do file locking
AC_CHECK_FUNCS([flock])
AC_CHECK_DECLS([O_EXLOCK],,, [[#include <fcntl.h>]])
AC_CHECK_DECLS([F_SETLK],,, [[#include <fcntl.h>]])
if test "x$ac_cv_func_flock" != "xyes" && \
   test "x$ac_cv_have_decl_O_EXLOCK" != "xyes" && \
   test "x$ac_cv_have_decl_F_SETLK" != "xyes"
then
  AC_MSG_ERROR([[cannot work out how to do file locking on your platform]])
fi

## Get tmpdir
temp_directory_name="/tmp"
AC_ARG_WITH(
  [tmp-dir],
  [AC_HELP_STRING([--with-tmp-dir=DIR], [Directory for temporary files [/tmp]])],
  [temp_directory_name="$withval"])
AC_DEFINE_UNQUOTED([TEMP_DIRECTORY_NAME], ["$temp_directory_name"], [TMP directory name])

## Allow linking binaries with static libraries
AC_ARG_ENABLE(
  [static-bin],
  [AC_HELP_STRING([--enable-static-bin], [Link binaries with static libraries])])
if test "x$enable_static_bin" = "xyes"; then
  AC_CHECK_LIB([ssl],[SSL_read])
  LIBS="-Wl,-Bstatic $LIBS -Wl,-Bdynamic"
fi


### Output files
AC_CONFIG_FILES([infrastructure/BoxPlatform.pm])
AC_OUTPUT

# Configure the Box build system
echo
perl ./infrastructure/makebuildenv.pl &&
  perl ./infrastructure/makeparcels.pl

# Write summary of important info
cat <<EOC
A summary of the build configuration is below. Box Backup will function
without these features, but will work better where they are present. Refer
to the documentation for more information on each feature.

Large files:         $have_large_file_support
Berkeley DB:         $ac_have_bdb
Readline:            $have_libreadline
Extended attributes: $ac_cv_header_sys_xattr_h
EOC


### Warnings at end for visibility

if test "x$gcc_3_plus" != "xyes" && test "x$malloc_workaround" != "xyes"; then
  echo
  AC_MSG_WARN([[the implementation of the C++ STL on this platform may
have a flaw which causes it to apparently leak memory, and this flaw cannot be
worked around.

When running the daemons, check their memory usage does not constantly
increase. The STL flaw can cause excessive memory use.]])
fi

case "$vl_cv_lib_readline" in
  *readline*)
    echo
    AC_MSG_WARN([[do not distribute binaries compiled against GNU readline,
as this would violate the GNU readline licence, which is GPL. You may use
libedit or libeditline instead.]])
    ;;
esac

if test "x$ac_have_bdb" != "xyes"; then
  echo
  AC_MSG_WARN([[db is not installed -- will run in reduced efficiency mode without it]])
fi
