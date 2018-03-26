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
dnl

AC_DEFUN(
  [AX_TRY_RUN_FD],
  [
    AC_REQUIRE([AX_FUNC_SYSCALL])dnl
    if test "x$ac_cv_header_sys_syscall_h" = "xyes"; then
      AC_TRY_RUN(
        [
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
          int main()
          {
          int fd = creat("lseektest", 0600);
          int res = 1;
          if(fd>=0)
          {
            $1
            close(fd);
          }
          unlink("lseektest");
          return abs(res);
          }
        ],
        [$2],
	[$3],
	[$3 # assume not for cross-compiling]
      )
    fi
  ])dnl

AC_DEFUN([AX_CHECK_SYSCALL_LSEEK_DUMMY_PARAM], [
  AC_CACHE_CHECK(
    [[whether syscall lseek requires dummy parameter]],
    [box_cv_have_lseek_dummy_param],
    [AX_TRY_RUN_FD(
      [
            // This test tries first to seek to position 0, with NO
            // "dummy argument". If lseek does actually require a dummy
            // argument, then it will eat SEEK_SET for the offset and
            // try to use 99 as whence, which is invalid, so res will be
            // 1, the program will return 1 and we will define
            // HAVE_LSEEK_DUMMY_PARAM
            // (whew! that took 1 hour to figure out)
            // The "dummy argument" probably means that it takes a 64-bit
            // offset.
            res = (syscall(SYS_lseek, fd, (int32_t)10, SEEK_SET, 99) == 10) ? 0 : 1;
      ],
      dnl if the test program succeeds (res == 0):
      [box_cv_have_lseek_dummy_param=no],
      dnl if the test program fails (res != 0):
      [box_cv_have_lseek_dummy_param=yes],
    )]
  )
  if test "x$box_cv_have_lseek_dummy_param" != "xno"; then
    AC_DEFINE([HAVE_LSEEK_DUMMY_PARAM], 1,
              [Define to 1 if syscall lseek requires a dummy middle parameter])
  fi
])

# Please note: this does not appear to work! Do not rely on it without further testing:
AC_DEFUN([AX_CHECK_SYSCALL_LSEEK_64_BIT], [
  AC_CACHE_CHECK(
    [[whether syscall lseek takes a 64-bit offset parameter]],
    [box_cv_have_lseek_64_bit],
    [AX_TRY_RUN_FD(
      [
            // Try to seek to a position that requires a 64-bit representation, and check the return
            // value, which is the current position.
            if(syscall(SYS_lseek, fd, (int64_t)5, SEEK_SET) != 5)
            {
              res = 2;
            }
            else if(syscall(SYS_lseek, fd, (int64_t)10, SEEK_CUR) != 15)
            {
              res = 3;
            }
            else
            {
              res = 0;
            }
      ],
      dnl if the test program succeeds (res == 0):
      [box_cv_have_lseek_64_bit=yes],
      dnl if the test program fails (res != 0):
      [box_cv_have_lseek_64_bit=no]
    )]
  )
  if test "x$box_cv_have_lseek_64_bit" != "xno"; then
    AC_DEFINE([HAVE_LSEEK_64_BIT], 1,
              [Define to 1 if syscall lseek takes a 64-bit offset])
  fi
])
