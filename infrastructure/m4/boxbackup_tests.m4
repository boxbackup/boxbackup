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

# Enable some compiler flags if the compiler supports them. This gives better warnings
# and detects some problems early.
AX_CHECK_COMPILE_FLAG(-Wall, [cxxflags_strict="$cxxflags_strict -Wall"])
# -Wundef would be a good idea, but Boost is full of undefined variable use, so we need
# to disable it for now so that we can concentrate on real errors:
dnl AX_CHECK_COMPILE_FLAG(-Wundef, [cxxflags_strict="$cxxflags_strict -Wundef"])
AX_CHECK_COMPILE_FLAG(-Werror=return-type,
	[cxxflags_strict="$cxxflags_strict -Werror=return-type"])
AX_CHECK_COMPILE_FLAG(-Werror=non-virtual-dtor,
	[cxxflags_strict="$cxxflags_strict -Werror=non-virtual-dtor"])
AX_CHECK_COMPILE_FLAG(-Werror=delete-non-virtual-dtor,
	[cxxflags_strict="$cxxflags_strict -Werror=delete-non-virtual-dtor"])
AX_CHECK_COMPILE_FLAG(-Werror=parentheses,
	[cxxflags_strict="$cxxflags_strict -Werror=parentheses"])
# We should really enable -Werror=sometimes-uninitialized, but QDBM violates it:
dnl AX_CHECK_COMPILE_FLAG(-Werror=sometimes-uninitialized,
dnl 	[cxxflags_strict="$cxxflags_strict -Werror=sometimes-uninitialized"])
AX_CHECK_COMPILE_FLAG(-Werror=overloaded-virtual,
	[cxxflags_strict="$cxxflags_strict -Werror=overloaded-virtual"])
AC_SUBST([CXXFLAGS_STRICT], [$cxxflags_strict])

if test "x$GXX" = "xyes"; then
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
    save_LDFLAGS=$LDFLAGS
    LDFLAGS="$LDFLAGS -rdynamic"
    AC_MSG_CHECKING([whether gcc accepts -rdynamic])
    AC_TRY_LINK([], [return 0;],
      [AC_MSG_RESULT([yes]); have_rdynamic=yes],
      [AC_MSG_RESULT([no])])
    if test x"$have_rdynamic" = x"yes" ; then
      AC_SUBST([LDADD_RDYNAMIC], ['-rdynamic'])
    fi
    LDFLAGS=$save_LDFLAGS
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

# need to find libdl before trying to link openssl, apparently
AC_SEARCH_LIBS([dlsym], [dl])
AC_CHECK_FUNCS([dlsym dladdr])

## Check for Open SSL, use old versions only if explicitly requested
AC_SEARCH_LIBS([gethostbyname], [nsl socket resolv])
AC_SEARCH_LIBS([shutdown], [nsl socket resolv])
AX_CHECK_SSL(, [AC_MSG_ERROR([[OpenSSL is not installed but is required]])])
AC_ARG_ENABLE(
  [old-ssl],
  [AC_HELP_STRING([--enable-old-ssl],
                  [Allow use of pre-0.9.7 Open SSL - NOT RECOMMENDED, read the documentation])])
AC_SEARCH_LIBS(
  [EVP_CipherInit_ex],
  [crypto],, [
  if test "x$enable_old_ssl" = "xyes"; then
    AC_DEFINE([HAVE_OLD_SSL], 1, [Define to 1 if SSL is pre-0.9.7])
  else
    AC_MSG_ERROR([[found an old (pre 0.9.7) version of SSL.
Upgrade or read the documentation for alternatives]])
  fi
  ])


### Checks for header files.

AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([cxxabi.h dirent.h dlfcn.h fcntl.h getopt.h lmcons.h netdb.h process.h pwd.h])
AC_CHECK_HEADERS([signal.h syslog.h time.h unistd.h])
AC_CHECK_HEADERS([netinet/in.h netinet/tcp.h])
AC_CHECK_HEADERS([sys/file.h sys/param.h sys/poll.h sys/socket.h sys/stat.h sys/time.h])
AC_CHECK_HEADERS([sys/types.h sys/uio.h sys/un.h sys/wait.h sys/xattr.h])
AC_CHECK_HEADERS([sys/ucred.h],,, [
	#ifdef HAVE_SYS_PARAM_H
	#	include <sys/param.h>
	#endif
	])
AC_CHECK_HEADERS([bsd/unistd.h])
AC_CHECK_HEADERS([sys/socket.h], [have_sys_socket_h=yes])
AC_CHECK_HEADERS([winsock2.h], [have_winsock2_h=yes])
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

# Check for Boost PropertyTree (XML and JSON support for lib/httpserver)
AX_BOOST_BASE(,
	# ax_check_boost.m4 thwarts our attempts to modify CPPFLAGS and
	# LDFLAGS by restoring them AFTER running ACTION-IF-FOUND. But we
	# can fight back by updating the _SAVED variables instead, and use
	# the fact that we know that CPPFLAGS and LDFLAGS are still set with
	# the correct values for Boost, to preserve them by overwriting
	# CPPFLAGS_SAVED and LDFLAGS_SAVED.
	[CPPFLAGS_SAVED="$CPPFLAGS"
	 LDFLAGS_SAVED="$LDFLAGS"],
	[AC_MSG_ERROR([[cannot find Boost, try installing libboost-dev]])])

AC_CHECK_HEADER([boost/property_tree/ptree.hpp],,
	[AC_MSG_ERROR([[cannot find Boost::PropertyTree, try installing libboost-dev]])])

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

AC_CHECK_MEMBERS([struct stat.st_flags],,, [[#include <sys/stat.h>]])
AC_CHECK_MEMBERS([struct stat.st_atim],,, [[#include <sys/stat.h>]])
AC_CHECK_MEMBERS([struct stat.st_atimespec],,, [[#include <sys/stat.h>]])
AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec],,, [[#include <sys/stat.h>]])
AC_CHECK_MEMBERS([struct stat.st_atimensec],,, [[#include <sys/stat.h>]])
AC_CHECK_MEMBERS([struct sockaddr_in.sin_len],,, [[
  #include <sys/types.h>
  #include <netinet/in.h>
  ]])
AC_CHECK_MEMBERS([DIR.d_fd],,,  [[#include <dirent.h>]])
AC_CHECK_MEMBERS([DIR.dd_fd],,, [[#include <dirent.h>]])
AC_CHECK_MEMBERS([struct tcp_info.tcpi_rtt],,, [[#include <netinet/tcp.h>]])

AC_CHECK_DECLS([O_BINARY],,, [[#include <fcntl.h>]])

AC_CHECK_DECLS([ENOTSUP],,, [[#include <sys/errno.h>]])
AC_CHECK_DECLS([INFTIM],,, [[#include <poll.h>]])
AC_CHECK_DECLS([SO_PEERCRED],,, [[#include <sys/socket.h>]])
AC_CHECK_DECLS([SOL_TCP],,, [[#include <netinet/tcp.h>]])
AC_CHECK_DECLS([TCP_INFO],,, [[#include <netinet/tcp.h>]])

if test -n "$have_sys_socket_h"; then
  AC_CHECK_DECLS([SO_SNDBUF],,, [[#include <sys/socket.h>]])
elif test -n "$have_winsock2_h"; then
  AC_CHECK_DECLS([SO_SNDBUF],,, [[#include <winsock2.h>]])
else
  # unlikely to succeed, but defined HAVE_DECL_SO_SNDBUF to 0 instead
  # of leaving it undefined, which makes cpp #ifdefs simpler.
  AC_CHECK_DECLS([SO_SNDBUF])
fi

# Solaris provides getpeerucred() instead of getpeereid() or SO_PEERCRED
AC_CHECK_HEADERS([ucred.h])
AC_CHECK_FUNCS([getpeerucred])
AC_CHECK_MEMBERS([struct ucred.uid, struct ucred.cr_uid],,,
	[[
		#ifdef HAVE_UCRED_H
		#	include <ucred.h>
		#endif

		#ifdef HAVE_SYS_PARAM_H
		#	include <sys/param.h>
		#endif

		#ifdef HAVE_SYS_UCRED_H
		#	include <sys/ucred.h>
		#endif

		#ifdef HAVE_SYS_SOCKET_H
		#	include <sys/socket.h>
		#endif
	]])

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
AC_CHECK_FUNCS([ftruncate getpeereid getpeername getpid gettimeofday lchown])
AC_CHECK_FUNCS([setproctitle utimensat])
AC_SEARCH_LIBS([setproctitle], [bsd])

# NetBSD implements kqueue too differently for us to get it fixed by 0.10
# TODO: Remove this when NetBSD kqueue implementation is working. The main
# thing to fix is that ServerStream needs to put a pointer into WaitForEvent,
# which wants to store it in struct kevent.udata, but on NetBSD that's an
# intptr_t instead of a void *, and it doesn't like accepting pointers.
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

AC_CHECK_DECLS([GetUserNameA],,, [[#include <windows.h>]])

AC_CHECK_PROGS(default_debugger, [lldb gdb])
AC_ARG_WITH([debugger],
            [AS_HELP_STRING([--with-debugger=<gdb|lldb|...>],
              [use this debugger in t-gdb scripts to debug tests @<:@default=lldb if present, otherwise gdb@:>@])],
            [],
            [with_debugger=$default_debugger])
AC_SUBST([with_debugger])
