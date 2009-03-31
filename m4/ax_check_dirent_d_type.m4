dnl @synopsis AX_CHECK_DIRENT_D_TYPE([ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl
dnl This macro will find out if struct dirent.d_type is present and supported.
dnl
dnl The following defines will be set as appropriate:
dnl HAVE_STRUCT_DIRENT_D_TYPE
dnl HAVE_VALID_DIRENT_D_TYPE
dnl Also ACTION-IF-TRUE and ACTION-IF-FALSE are run as appropriate
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/03
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_DIRENT_D_TYPE], [
  AC_CHECK_MEMBERS([struct dirent.d_type],,, [[#include <dirent.h>]])
  if test "x$ac_cv_member_struct_dirent_d_type" = "xyes"; then
    AC_CACHE_CHECK([[whether struct dirent.d_type is valid]], [box_cv_have_valid_dirent_d_type],
      [AC_TRY_RUN(
        [AC_LANG_PROGRAM([[
          $ac_includes_default
          #include <dirent.h>
          ]], [[
          DIR* dir = opendir(".");
          struct dirent* res = NULL;
          if(dir) res = readdir(dir);
          return res ? (res->d_type != DT_FILE && res->d_type != DT_DIR) : 1;
        ]])],
        [box_cv_have_valid_dirent_d_type=yes],
	[box_cv_have_valid_dirent_d_type=no],
	[box_cv_have_valid_dirent_d_type=cross]
      )])
    if test "x$box_cv_have_valid_dirent_d_type" = "xyes"; then
      AC_DEFINE([HAVE_VALID_DIRENT_D_TYPE], 1, [Define to 1 if struct dirent.d_type is valid])
    fi
  fi
  if test "x$ac_cv_member_struct_dirent_d_type" = "xyes" || \
     test "x$box_cv_have_valid_dirent_d_type" = "xyes"
  then
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
