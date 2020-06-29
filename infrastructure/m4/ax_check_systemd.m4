
AC_DEFUN([AX_CHECK_SYSTEMD], [
  
  ax_check_systemd_found=yes
  AC_CHECK_HEADERS([systemd/sd-daemon.h],, [ax_check_systemd_found=no])
  AC_SEARCH_LIBS([sd_notifyf], [systemd],, [ax_check_systemd_found=no])

  if test "x$ax_check_systemd_found" = "xyes"; then
    AC_DEFINE([HAVE_SYSTEMD], 1, [Define to 1 if SYSTEMD is available])
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
  