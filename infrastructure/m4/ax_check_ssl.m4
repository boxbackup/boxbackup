dnl @synopsis AX_CHECK_SSL([ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl
dnl This macro will check for OpenSSL in the standard path, allowing the user
dnl to specify a directory if it is not found. The user uses
dnl '--with-ssl-headers=/path/to/headers' or
dnl '--with-ssl-lib=/path/to/lib' as arguments to configure.
dnl
dnl If OpenSSL is found the include directory gets added to CPPFLAGS,
dnl '-lcrypto', '-lssl', and the libraries directory are added to LDFLAGS.
dnl Also HAVE_SSL is defined to 1, and ACTION-IF-TRUE and ACTION-IF-FALSE are
dnl run as appropriate
dnl
dnl @category InstalledPackages
dnl @author Martin Ebourne
dnl @version 2005/07/01
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_SSL], [
  AC_ARG_WITH(
    [ssl-headers],
    [AC_HELP_STRING([--with-ssl-headers=DIR], [SSL include files location])],
    [ CPPFLAGS="$CPPFLAGS -I$withval"
      ax_check_ssl_headers="--with-ssl-headers=\"$withval\"" ])
  AC_ARG_WITH(
    [ssl-lib],
    [AC_HELP_STRING([--with-ssl-lib=DIR], [SSL library location])],
    [ LDFLAGS="$LDFLAGS -L$withval" 
      ax_check_ssl_lib="--with-ssl-lib=\"$withval\"" ])

  ax_check_ssl_found=yes
  AC_CHECK_HEADERS([openssl/ssl.h],, [ax_check_ssl_found=no])
  AC_CHECK_LIB([ssl],    [SSL_read],, [ax_check_ssl_found=no], [-lcrypto])
  AC_CHECK_LIB([crypto], [BIO_read],, [ax_check_ssl_found=no])

  if test "x$ax_check_ssl_found" = "xyes"; then
    AC_DEFINE([HAVE_SSL], 1, [Define to 1 if SSL is available])
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
