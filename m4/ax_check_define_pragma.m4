dnl @synopsis AX_CHECK_DEFINE_PRAGMA([ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl
dnl This macro will find out if the compiler will accept #pragma inside a
dnl #define. HAVE_DEFINE_PRAGMA will be defined if this is the case, and
dnl ACTION-IF-TRUE and ACTION-IF-FALSE are run as appropriate
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/03
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_DEFINE_PRAGMA], [
  AC_CACHE_CHECK([for pre-processor pragma defines], [box_cv_have_define_pragma],
    [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
        #define TEST_DEFINE #pragma pack(1)
        TEST_DEFINE
      ]])],
      [box_cv_have_define_pragma=yes], [box_cv_have_define_pragma=no]
    )])
  if test "x$box_cv_have_define_pragma" = "xyes"; then
    AC_DEFINE([HAVE_DEFINE_PRAGMA], 1, [Define to 1 if #define of pragmas works])
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
