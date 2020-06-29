
AC_DEFUN([AX_CHECK_PTHREAD], [
  
  ax_check_pthread_found=yes
  AC_CHECK_HEADERS([pthread.h],, [ax_check_pthread_found=no])
  AC_SEARCH_LIBS([pthread_create], [pthread],, [ax_check_pthread_found=no])

  if test "x$ax_check_systemd_found" = "xyes"; then
    AC_DEFINE([HAVE_PTHREAD], 1, [Define to 1 if PTHREAD is available])
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
  