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

AC_DEFUN([AX_FUNC_SYSCALL], [
  AC_CHECK_HEADERS([sys/syscall.h unistd.h])
  AC_CHECK_FUNCS([syscall __syscall])
  if test "x$ac_cv_func_syscall" != "xyes" &&
     test "x$ac_cv_func___syscall" != "xyes"; then
    AC_CACHE_CHECK([for __syscall needing definition], [have___syscall_need_defn],
      [AC_RUN_IFELSE([AC_LANG_PROGRAM([[
          $ac_includes_default
          #ifdef HAVE_SYS_SYSCALL_H
            #include <sys/syscall.h>
          #endif
          extern "C" off_t __syscall(quad_t number, ...);
          ]], [[
          __syscall(SYS_exit, 0);
          return 1;
        ]])],
        [have___syscall_need_defn=yes], [have___syscall_need_defn=no]
      )])
    if test "x$have___syscall_need_defn" = "xyes"; then
      AC_DEFINE([HAVE___SYSCALL_NEED_DEFN], 1,
                [Define to 1 if __syscall is available but needs a definition])
    fi
  fi
  ])dnl
