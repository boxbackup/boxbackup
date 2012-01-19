dnl @synopsis AX_CHECK_NONALIGNED_ACCESS
dnl
dnl This macro will see if non-aligned memory accesses will fail. The following
dnl defines will be made as appropriate:
dnl HAVE_ALIGNED_ONLY_INT16
dnl HAVE_ALIGNED_ONLY_INT32
dnl HAVE_ALIGNED_ONLY_INT64
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/12
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_NONALIGNED_ACCESS], [
  AC_CACHE_CHECK([if non-aligned 16 bit word accesses fail], [box_cv_have_aligned_only_int16],
    [AC_RUN_IFELSE([AC_LANG_PROGRAM([[$ac_includes_default]], [[
        #ifndef HAVE_UINT16_T
          #define uint16_t u_int16_t;
        #endif
        uint16_t scratch[2];
        memset(scratch, 0, sizeof(scratch));
        return *(uint16_t*)((char*)scratch+1);
      ]])],
      [box_cv_have_aligned_only_int16=no], [box_cv_have_aligned_only_int16=yes]
    )])
  if test "x$box_cv_have_aligned_only_int16" = "xyes"; then
    AC_DEFINE([HAVE_ALIGNED_ONLY_INT16], 1, [Define to 1 if non-aligned int16 access will fail])
  fi
  AC_CACHE_CHECK([if non-aligned 32 bit word accesses fail], [box_cv_have_aligned_only_int32],
    [AC_RUN_IFELSE([AC_LANG_PROGRAM([[$ac_includes_default]], [[
        #ifndef HAVE_UINT32_T
          #define uint32_t u_int32_t;
        #endif
        uint32_t scratch[2];
        memset(scratch, 0, sizeof(scratch));
        return *(uint32_t*)((char*)scratch+1);
      ]])],
      [box_cv_have_aligned_only_int32=no], [box_cv_have_aligned_only_int32=yes]
    )])
  if test "x$box_cv_have_aligned_only_int32" = "xyes"; then
    AC_DEFINE([HAVE_ALIGNED_ONLY_INT32], 1, [Define to 1 if non-aligned int32 access will fail])
  fi
  AC_CACHE_CHECK([if non-aligned 64 bit word accesses fail], [box_cv_have_aligned_only_int64],
    [AC_RUN_IFELSE([AC_LANG_PROGRAM([[$ac_includes_default]], [[
        #ifndef HAVE_UINT64_T
          #define uint64_t u_int64_t;
        #endif
        uint64_t scratch[2];
        memset(scratch, 0, sizeof(scratch));
        return *(uint64_t*)((char*)scratch+1);
      ]])],
      [box_cv_have_aligned_only_int64=no], [box_cv_have_aligned_only_int64=yes]
    )])
  if test "x$box_cv_have_aligned_only_int64" = "xyes"; then
    AC_DEFINE([HAVE_ALIGNED_ONLY_INT64], 1, [Define to 1 if non-aligned int64 access will fail])
  fi
  ])dnl
