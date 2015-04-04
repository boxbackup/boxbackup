dnl @synopsis AX_BSWAP64
dnl
dnl This macro will check for a built in way of endian reversing an int64_t.
dnl If one is found then HAVE_BSWAP64 is set to 1 and BSWAP64 will be defined
dnl to the name of the endian swap function.
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2006/02/02
dnl @license AllPermissive

AC_DEFUN([AX_BSWAP64], [
  bswap64_function=""
  AC_CHECK_HEADERS([sys/endian.h asm/byteorder.h])
  if test "x$ac_cv_header_sys_endian_h" = "xyes"; then
    AC_CACHE_CHECK([for htobe64], [box_cv_have_htobe64],
      [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
          $ac_includes_default
          #include <sys/endian.h>
          ]], [[
          htobe64(0);
          return 1;
        ]])],
        [box_cv_have_htobe64=yes], [box_cv_have_htobe64=no]
      )])
    if test "x$box_cv_have_htobe64" = "xyes"; then
      bswap64_function=htobe64
    fi
  fi
  if test "x$bswap64_function" = "x" && \
     test "x$ac_cv_header_asm_byteorder_h" = "xyes"; then
    AC_CACHE_CHECK([for __cpu_to_be64], [box_cv_have___cpu_to_be64],
      [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
          $ac_includes_default
          #include <asm/byteorder.h>
          ]], [[
          __cpu_to_be64(0);
          return 1;
        ]])],
        [box_cv_have___cpu_to_be64=yes], [box_cv_have___cpu_to_be64=no]
      )])
    if test "x$box_cv_have___cpu_to_be64" = "xyes"; then
      bswap64_function=__cpu_to_be64
    fi
  fi

  if test "x$bswap64_function" != "x"; then
    AC_DEFINE([HAVE_BSWAP64], 1,
              [Define to 1 if BSWAP64 is defined to the name of a valid 64 bit endian swapping function])
    AC_DEFINE_UNQUOTED([BSWAP64], [$bswap64_function], [Name of the 64 bit endian swapping function])
  fi
  ])dnl
