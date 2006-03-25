#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([Box Backup], 0.10, [box@fluffy.co.uk])
AC_CONFIG_SRCDIR([lib/common/Box.h])
AC_CONFIG_HEADERS([lib/common/BoxConfig.h])

touch install-sh
AC_CANONICAL_SYSTEM
test -s install-sh || rm install-sh

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
AC_PATH_PROG([PERL], [perl], [no])
if test "x$PERL" != "xno"; then
  AC_DEFINE_UNQUOTED([PERL_EXECUTABLE], ["$PERL"], [Location of the perl executable])
else
  AC_MSG_ERROR([[perl executable was not found]])
fi


### Checks for libraries.

if test "$target_os" != "mingw32" -a "$target_os" != "winnt"; then
  AC_SEARCH_LIBS([nanosleep], [rt], [ac_have_nanosleep=yes],
                 [AC_MSG_ERROR([[cannot find a short sleep function (nanosleep)]])])
fi
AC_CHECK_LIB([z], [zlibVersion],, [AC_MSG_ERROR([[cannot find zlib]])])
VL_LIB_READLINE([have_libreadline=yes], [have_libreadline=no])

## Check for Berkely DB. Restrict to certain versions
AX_PATH_BDB(, [
  LIBS="$BDB_LIBS $LIBS"
  LDFLAGS="$BDB_LDFLAGS $LDFLAGS"
  CPPFLAGS="$CPPFLAGS $BDB_CPPFLAGS"

  AX_COMPARE_VERSION([$BDB_VERSION],[ge],[4.1],,
    [AX_COMPARE_VERSION([$BDB_VERSION],[lt],[2],,
      [AC_MSG_ERROR([[only Berkely DB versions 1.x or at least 4.1 are currently supported]])]
      )]
  )
  AX_SPLIT_VERSION([BDB_VERSION], [$BDB_VERSION])
])

## Check for Open SSL, use old versions only if explicitly requested
AC_SEARCH_LIBS([gethostbyname], [nsl socket resolv])
AC_SEARCH_LIBS([shutdown], [nsl socket resolv])
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

if test "$target_os" != "mingw32"; then
  AC_HEADER_DIRENT
fi

AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([execinfo.h process.h pwd.h regex.h signal.h])
AC_CHECK_HEADERS([syslog.h time.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([sys/param.h sys/socket.h sys/time.h sys/types.h sys/wait.h])
AC_CHECK_HEADERS([sys/xattr.h])
AC_CHECK_HEADERS([sys/endian.h asm/byteorder.h])

if test "$ac_cv_header_regex_h" = "yes"; then
  AC_SEARCH_LIBS([regcomp], [pcreposix])
fi

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
if test "x$ac_cv_c_bigendian" != "xyes"; then
  AX_BSWAP64
fi
if test "$target_os" != "mingw32"; then
  AX_RANDOM_DEVICE
fi
AX_CHECK_MOUNT_POINT(,[
  if test "$target_os" != "mingw32" -a "$target_os" != "winnt"; then
    AC_MSG_ERROR([[cannot work out how to discover mount points on your platform]])
  fi
  ])
AX_CHECK_MALLOC_WORKAROUND


### Checks for library functions.

AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([getpeereid lchown setproctitle getpid])
# NetBSD implements kqueue too differently for us to get it fixed by 0.10
# TODO: Remove this when NetBSD kqueue implementation is working
netbsd_hack=`echo $target_os | sed 's/netbsd.*/netbsd/'`
if test "$netbsd_hack" != "netbsd"; then
  AC_CHECK_FUNCS([kqueue])
fi
AX_FUNC_SYSCALL
AX_CHECK_SYSCALL_LSEEK
AC_CHECK_FUNCS([listxattr llistxattr getxattr lgetxattr setxattr lsetxattr])
AC_CHECK_DECLS([XATTR_NOFOLLOW],,, [[#include <sys/xattr.h>]])


### Miscellaneous complicated feature checks

## Check for large file support active. AC_SYS_LARGEFILE has already worked
## out how to enable it if necessary, we just use this to report to the user
AC_CACHE_CHECK([if we have large file support enabled], [have_large_file_support],
  [AC_RUN_IFELSE([AC_LANG_PROGRAM([[$ac_includes_default]], [[
      return sizeof(off_t)==4;
    ]])],
    [have_large_file_support=yes], [have_large_file_support=no]
  )])

## Find out how to do file locking
AC_CHECK_FUNCS([flock])
AC_CHECK_DECLS([O_EXLOCK],,, [[#include <fcntl.h>]])
AC_CHECK_DECLS([F_SETLK],,, [[#include <fcntl.h>]])
if test "x$ac_cv_func_flock" != "xyes" && \
   test "x$ac_cv_have_decl_O_EXLOCK" != "xyes" && \
   test "x$ac_cv_have_decl_F_SETLK" != "xyes" && \
   test "$target_os" != "mingw32" -a "$target_os" != "winnt"
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
  AC_CHECK_LIB([ssl],[SSL_read],,, [crypto])
  LIBS="-Wl,-Bstatic $LIBS -Wl,-Bdynamic"
fi

## Kludge to allow makeparcels.pl to use bindir. This is not a good long term
## solution because it prevents use of "make exec_prefix=/some/dir"
saved_prefix=$prefix
saved_exec_prefix=$exec_prefix
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
bindir_expanded=`eval "echo $bindir"`
bindir_expanded=`eval "echo $bindir_expanded"`
prefix=$saved_prefix
exec_prefix=$saved_exec_prefix
AC_SUBST([bindir_expanded])


### Output files
AC_CONFIG_FILES([infrastructure/BoxPlatform.pm])
AX_CONFIG_SCRIPTS([bin/bbackupd/bbackupd-config
                   bin/bbackupquery/makedocumentation.pl
                   bin/bbstored/bbstored-certs
                   bin/bbstored/bbstored-config
                   infrastructure/makebuildenv.pl
                   infrastructure/makeparcels.pl
                   lib/common/makeexception.pl
                   lib/raidfile/raidfile-config
                   lib/server/makeprotocol.pl
                   runtest.pl
                   test/backupstorefix/testfiles/testbackupstorefix.pl
                   test/bbackupd/testfiles/extcheck1.pl
                   test/bbackupd/testfiles/extcheck2.pl
                   test/bbackupd/testfiles/notifyscript.pl])
# TODO: Need to do contrib/cygwin/install-cygwin-service.pl but location varies
AC_OUTPUT

# Configure the Box build system
echo
$PERL ./infrastructure/makebuildenv.pl &&
  $PERL ./infrastructure/makeparcels.pl

# Write summary of important info
cat <<EOC
A summary of the build configuration is below. Box Backup will function
without these features, but will work better where they are present. Refer
to the documentation for more information on each feature.

Large files:         $have_large_file_support
Berkeley DB:         $ax_path_bdb_ok
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
as this could violate the GNU readline licence, which is GPL. You may use
libedit or libeditline instead.]])
    ;;
esac
