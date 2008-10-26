dnl @synopsis AX_FUNC_SYSCALL
dnl
dnl This macro will find out how to call syscall. One or more of the following
dnl defines will be made as appropriate:
dnl HAVE_UNISTD_H            - If unistd.h is available
dnl HAVE_SYS_SYSCALL_H       - If sys/syscall.h is available
dnl HAVE_SYSCALL             - If syscall() is available and is defined in unistd.h
dnl HAVE___SYSCALL           - If __syscall() is available and is defined in unistd.h
dnl HAVE___SYSCALL_NEED_DEFN - If __syscall() is available but is not defined in unistd.h
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/01
dnl @license AllPermissive
dnl
dnl Changed by Chris on 081026:
dnl
dnl Reversed the test for __syscall(), remove the test for syscall(),
dnl remove the definition and reverse the sense in ax_func_syscall.m4
dnl (which checks for __syscall() needing definition).
dnl
dnl Autoconf's AC_CHECK_FUNC defines it when testing for its presence,
dnl so HAVE___SYSCALL will be true even if __syscall has no definition
dnl in the system libraries, and this is precisely the case that we
dnl want to test for, so now we test whether the test program compiles
dnl with no explicit definition (only the system headers) and if that
dnl fails, we set HAVE___SYSCALL_NEED_DEFN to 1.

AC_DEFUN([AX_FUNC_SYSCALL], [
  AC_CHECK_HEADERS([sys/syscall.h unistd.h])
  AC_CHECK_FUNCS([syscall __syscall])
  if test "x$ac_cv_func___syscall" = "xyes"; then
    AC_CACHE_CHECK([for __syscall needing definition], [box_cv_have___syscall_need_defn],
      [AC_RUN_IFELSE([AC_LANG_PROGRAM([[
          $ac_includes_default
          #ifdef HAVE_SYS_SYSCALL_H
            #include <sys/syscall.h>
          #endif
          ]], [[
          __syscall(SYS_exit, 0);
          return 1;
        ]])],
        [box_cv_have___syscall_need_defn=no], [box_cv_have___syscall_need_defn=yes]
      )])
    if test "x$box_cv_have___syscall_need_defn" = "xyes"; then
      AC_DEFINE([HAVE___SYSCALL_NEED_DEFN], 1,
                [Define to 1 if __syscall is available but needs a definition])
    fi
  fi
  ])dnl
