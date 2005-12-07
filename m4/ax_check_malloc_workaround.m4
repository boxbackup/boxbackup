dnl @synopsis AX_CHECK_MALLOC_WORKAROUND
dnl
dnl This macro will see if there is a potential STL memory leak, and if we can
dnl work around it will define __USE_MALLOC as the fix.
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/12
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_MALLOC_WORKAROUND], [
  if test "x$GXX" = "xyes"; then
    AC_CACHE_CHECK([for gcc version 3 or later], [gcc_3_plus],
      [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
          #if __GNUC__ < 3
          #error "Old GNU C"
          #endif
          ]])],
      [gcc_3_plus=yes], [gcc_3_plus=no]
    )])
    if test "x$gcc_3_plus" = "xno"; then
      AC_CACHE_CHECK([for malloc workaround], [malloc_workaround],
        [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
            #define __USE_MALLOC
            #include <string>
          ]], [[
            std::string s;
            s = "test";
        ]])],
        [malloc_workaround=yes], [malloc_workaround=no]
      )])
      if test "x$malloc_workaround" = "xyes"; then
        AC_DEFINE([__USE_MALLOC], 1,
                  [Define to 1 if __USE_MALLOC is required work around STL memory leaks])
      fi
    fi
  fi
  ])dnl
