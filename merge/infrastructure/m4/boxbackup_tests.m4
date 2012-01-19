dnl All Box Backup configury magic is here, to be shared with Boxi

case $build_os in
solaris*)
  isa_bits=`isainfo -b`
  AC_MSG_NOTICE([setting compiler to use -m$isa_bits on Solaris])
  CFLAGS="$CFLAGS -m$isa_bits"
  CXXFLAGS="$CXXFLAGS -m$isa_bits"
  LDFLAGS="$LDFLAGS -m$isa_bits"
  ;;
esac

if test "x$GXX" = "xyes"; then
  # Use -Wall if we have gcc. This gives better warnings
  AC_SUBST([CXXFLAGS_STRICT], ['-Wall -Wundef'])

  # Don't check for gcc -rdynamic on Solaris as it's broken, but returns 0.
  # On Cygwin it does nothing except cause gcc to emit a warning message.
  case $build_os in
  solaris*|cygwin)
    AC_MSG_NOTICE([skipping check for -rdynamic on $build_os])
    ;;
  *)
    # Check whether gcc supports -rdynamic, thanks to Steve Ellcey
    # [http://readlist.com/lists/gcc.gnu.org/gcc/6/31502.html]
    # This is needed to get symbols in backtraces.
    # Note that this apparently fails on HP-UX and Solaris
    LDFLAGS="$LDFLAGS -rdynamic"
    AC_MSG_CHECKING([whether gcc accepts -rdynamic])
    AC_TRY_LINK([], [return 0;],
      [AC_MSG_RESULT([yes]); have_rdynamic=yes],
      [AC_MSG_RESULT([no])])
    if test x"$have_rdynamic" = x"yes" ; then
      AC_SUBST([LDADD_RDYNAMIC], ['-rdynamic'])
    fi
    ;;
  esac
fi

AC_PATH_PROG([PERL], [perl], [AC_MSG_ERROR([[perl executable was not found]])])

case $target_os in
mingw*) 
	TARGET_PERL=perl
	;;
*)
	TARGET_PERL=$PERL
	;;
esac

AC_SUBST([TARGET_PERL])
AC_DEFINE_UNQUOTED([PERL_EXECUTABLE], ["$TARGET_PERL"], 
	[Location of the perl executable])

AC_CHECK_TOOL([AR],     [ar],    
	[AC_MSG_ERROR([[cannot find ar executable]])])
AC_CHECK_TOOL([RANLIB], [ranlib],
	[AC_MSG_ERROR([[cannot find ranlib executable]])])

case $target_os in
mingw*) 
	AC_CHECK_TOOL([WINDRES], [windres],
		[AC_MSG_ERROR([[cannot find windres executable]])])
	;;
esac

### Checks for libraries.

case $target_os in
mingw32*)
	AC_CHECK_LIB([crypto -lws2_32 -lgdi32], [CRYPTO_lock])
	;;
winnt)
	;;
*)
	AC_SEARCH_LIBS([nanosleep], [rt], [ac_have_nanosleep=yes],
		[AC_MSG_ERROR([[cannot find a short sleep function (nanosleep)]])])
	;;
esac

AC_CHECK_HEADER([zlib.h],, [AC_MSG_ERROR([[cannot find zlib.h]])])
AC_CHECK_LIB([z], [zlibVersion],, [AC_MSG_ERROR([[cannot find zlib]])])
VL_LIB_READLINE([have_libreadline=yes], [have_libreadline=no])
AC_CHECK_FUNCS([rl_filename_completion_function])

## Check for Berkely DB. Restrict to certain versions
AX_PATH_BDB([1.x or 4.1], [
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

case $target_os in
mingw32*) ;;
winnt*)   ;;
*)
  AC_HEADER_DIRENT
  ;;
esac

AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([dlfcn.h fcntl.h getopt.h process.h pwd.h signal.h])
AC_CHECK_HEADERS([syslog.h time.h cxxabi.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([sys/file.h sys/param.h sys/socket.h sys/time.h sys/types.h sys/wait.h])
AC_CHECK_HEADERS([sys/uio.h sys/xattr.h])
AC_CHECK_HEADERS([bsd/unistd.h])

AC_CHECK_HEADERS([execinfo.h], [have_execinfo_h=yes])

if test "$have_execinfo_h" = "yes"; then
  AC_SEARCH_LIBS([backtrace],[execinfo])
fi

AC_CHECK_HEADER([regex.h], [have_regex_h=yes])

if test "$have_regex_h" = "yes"; then
  AC_DEFINE([HAVE_REGEX_H], [1], [Define to 1 if regex.h is available])
else
  AC_CHECK_HEADER([pcreposix.h], [have_pcreposix_h=yes])
fi

if test "$have_pcreposix_h" = "yes"; then
  AC_DEFINE([PCRE_STATIC], [1], [Box Backup always uses static PCRE])
  AC_SEARCH_LIBS([regcomp], ["pcreposix -lpcre"],,[have_pcreposix_h=no_regcomp])
fi

if test "$have_pcreposix_h" = "yes"; then
  AC_DEFINE([HAVE_PCREPOSIX_H], [1], [Define to 1 if pcreposix.h is available])
fi

if test "$have_regex_h" = "yes" -o "$have_pcreposix_h" = "yes"; then
  have_regex_support=yes
  AC_DEFINE([HAVE_REGEX_SUPPORT], [1], [Define to 1 if regular expressions are supported])
else
  have_regex_support=no
fi

AC_SEARCH_LIBS([dlsym], ["dl"])
AC_CHECK_FUNCS([dlsym dladdr])

### Checks for typedefs, structures, and compiler characteristics.

AC_CHECK_TYPES([u_int8_t, u_int16_t, u_int32_t, u_int64_t])
AC_CHECK_TYPES([uint8_t, uint16_t, uint32_t, uint64_t])

AC_HEADER_STDBOOL
AC_C_CONST
AC_C_BIGENDIAN
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

AC_CHECK_MEMBERS([struct stat.st_flags])
AC_CHECK_MEMBERS([struct stat.st_mtimespec])
AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_atimensec])
AC_CHECK_MEMBERS([struct sockaddr_in.sin_len],,, [[
  #include <sys/types.h>
  #include <netinet/in.h>
  ]])
AC_CHECK_MEMBERS([DIR.d_fd],,,  [[#include <dirent.h>]])
AC_CHECK_MEMBERS([DIR.dd_fd],,, [[#include <dirent.h>]])

AC_CHECK_DECLS([INFTIM],,, [[#include <poll.h>]])
AC_CHECK_DECLS([SO_PEERCRED],,, [[#include <sys/socket.h>]])
AC_CHECK_DECLS([O_BINARY],,,)

# Solaris provides getpeerucred() instead of getpeereid() or SO_PEERCRED
AC_CHECK_HEADERS([ucred.h])
AC_CHECK_FUNCS([getpeerucred])

AC_CHECK_DECLS([optreset],,, [[#include <getopt.h>]])
AC_CHECK_DECLS([dirfd],,,
	[[
		#include <getopt.h>
		#include <dirent.h>
	]])

AC_HEADER_TIME
AC_STRUCT_TM
AX_CHECK_DIRENT_D_TYPE
AC_SYS_LARGEFILE
AX_CHECK_DEFINE_PRAGMA
if test "x$ac_cv_c_bigendian" != "xyes"; then
  AX_BSWAP64
fi

case $target_os in
mingw32*) ;;
winnt*)   ;;
*)
  AX_RANDOM_DEVICE
  AX_CHECK_MOUNT_POINT(,[
    AC_MSG_ERROR([[cannot work out how to discover mount points on your platform]])
  ])
  AC_CHECK_MEMBERS([struct dirent.d_ino],,, [[#include <dirent.h>]])
;;
esac

AX_CHECK_MALLOC_WORKAROUND


### Checks for library functions.

AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([getpeereid lchown setproctitle getpid gettimeofday waitpid ftruncate])
AC_SEARCH_LIBS([setproctitle], ["bsd"])

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
AC_CACHE_CHECK([if we have large file support enabled],
  [box_cv_have_large_file_support],
  [AC_TRY_RUN([
    $ac_includes_default
    int main()
    {
      return sizeof(off_t)==4;
    }
    ],
    [box_cv_have_large_file_support=yes],
    [box_cv_have_large_file_support=no],
    [box_cv_have_large_file_support=no # safe for cross-compile]
    )
  ])

if test "x$box_cv_have_large_file_support" = "xyes"; then
	AC_DEFINE([HAVE_LARGE_FILE_SUPPORT], [1],
		[Define to 1 if large files are supported])
fi

## Find out how to do file locking
AC_CHECK_FUNCS([flock fcntl])
AC_CHECK_DECLS([O_EXLOCK],,, [[#include <fcntl.h>]])
AC_CHECK_DECLS([F_SETLK],,, [[#include <fcntl.h>]])

case $target_os in
mingw32*) ;;
winnt*)   ;;
*)
if test "x$ac_cv_func_flock" != "xyes" && \
   test "x$ac_cv_have_decl_O_EXLOCK" != "xyes" && \
   test "x$ac_cv_have_decl_F_SETLK" != "xyes"
then
  AC_MSG_ERROR([[cannot work out how to do file locking on your platform]])
fi
;;
esac


