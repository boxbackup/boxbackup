dnl @synopsis AX_CHECK_SYSCALL_LSEEK([ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl
dnl This macro will find out if the lseek syscall requires a dummy middle
dnl parameter
dnl
dnl The following defines will be set as appropriate:
dnl HAVE_LSEEK_DUMMY_PARAM
dnl Also ACTION-IF-TRUE and ACTION-IF-FALSE are run as appropriate
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/03
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_SYSCALL_LSEEK], [
  AC_REQUIRE([AX_FUNC_SYSCALL])dnl
  if test "x$ac_cv_header_sys_syscall_h" = "xyes"; then
    AC_CACHE_CHECK([[whether syscall lseek requires dummy parameter]], [box_cv_have_lseek_dummy_param],
      [AC_TRY_RUN(
        [AC_LANG_PROGRAM([[
          $ac_includes_default
          #include <fcntl.h>
          #include <sys/syscall.h>
          #ifdef HAVE___SYSCALL_NEED_DEFN
            extern "C" off_t __syscall(quad_t number, ...);
          #endif
          #ifdef HAVE___SYSCALL // always use it if we have it
            #undef syscall
            #define syscall __syscall
          #endif
          ]], [[
          int fh = creat("lseektest", 0600);
          int res = 0;
          if(fh>=0)
          {
            // This test tries first to seek to position 0, with NO
            // "dummy argument". If lseek does actually require a dummy
            // argument, then it will eat SEEK_SET for the offset and
            // try to use 99 as whence, which is invalid, so res will be
            // -1, the program will return zero and 
            // have_lseek_dummy_param=yes 
            // (whew! that took 1 hour to figure out)
            // The "dummy argument" probably means that it takes a 64-bit
            // offset, so this was probably a bug anyway, and now that
            // we cast the offset to off_t, it should never be needed
            // (if my reasoning is correct).
            res = syscall(SYS_lseek, fh, (off_t)0, SEEK_SET, 99);
            close(fh);
          }
          unlink("lseektest");
          return res!=-1;
        ]])],
        [box_cv_have_lseek_dummy_param=yes],
	[box_cv_have_lseek_dummy_param=no],
	[box_cv_have_lseek_dummy_param=no # assume not for cross-compiling]
      )])
    if test "x$box_cv_have_lseek_dummy_param" = "xyes"; then
      AC_DEFINE([HAVE_LSEEK_DUMMY_PARAM], 1,
                [Define to 1 if syscall lseek requires a dummy middle parameter])
    fi
  fi
  if test "x$box_cv_have_lseek_dummy_param" = "xno"
  then
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
