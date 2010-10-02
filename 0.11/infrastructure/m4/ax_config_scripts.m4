dnl @synopsis AX_CONFIG_SCRIPTS(SCRIPT_FILE, ...)
dnl
dnl Run AC_CONFIG_FILES on a list of scripts while preserving execute
dnl permission.
dnl
dnl @category Automake
dnl @author Martin Ebourne <martin@zepler.org>
dnl @script
dnl @license AllPermissive

AC_DEFUN([AX_CONFIG_SCRIPTS],[
  AC_REQUIRE([AC_CONFIG_FILES])dnl
  m4_foreach([SCRIPT_FILE],
             m4_quote(m4_split(m4_normalize([$1]))),
             [AC_CONFIG_FILES(SCRIPT_FILE, m4_quote(chmod +x SCRIPT_FILE))])dnl
])
