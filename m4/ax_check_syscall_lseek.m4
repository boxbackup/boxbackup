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
    AC_CACHE_CHECK([[whether syscall lseek requires dummy parameter]], [have_lseek_dummy_param],
      [AC_RUN_IFELSE(
        [AC_LANG_PROGRAM([[
          $ac_includes_default
          #include <fcntl.h>
          #include <sys/syscall.h>
          #ifdef HAVE___SYSCALL_NEED_DEFN
            extern "C" off_t __syscall(quad_t number, ...);
          #endif
          #ifndef HAVE_SYSCALL
            #undef syscall
            #define syscall __syscall
          #endif
          ]], [[
          int fh = creat("lseektest", 0600);
          int res = 0;
          if(fh>=0)
          {
            res = syscall(SYS_lseek, fh, 0, SEEK_SET, 99);
            close(fh);
          }
          unlink("lseektest");
          return res!=-1;
        ]])],
        [have_lseek_dummy_param=yes], [have_lseek_dummy_param=no]
      )])
    if test "x$have_lseek_dummy_param" = "xyes"; then
      AC_DEFINE([HAVE_LSEEK_DUMMY_PARAM], 1,
                [Define to 1 if syscall lseek requires a dummy middle parameter])
    fi
  fi
  if test "x$have_lseek_dummy_param" = "xno"
  then
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
