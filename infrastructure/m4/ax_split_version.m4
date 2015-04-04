dnl @synopsis AX_SPLIT_VERSION(DEFINE, VERSION)
dnl
dnl Splits a version number in the format MAJOR.MINOR.POINT into it's
dnl separate components and AC_DEFINES <DEFINE>_MAJOR etc with the values.
dnl
dnl @category Automake
dnl @author Martin Ebourne <martin@zepler.org>
dnl @version
dnl @license AllPermissive

AC_DEFUN([AX_SPLIT_VERSION],[
  ax_major_version=`echo "$2" | sed 's/\([[^.]][[^.]]*\).*/\1/'`
  ax_minor_version=`echo "$2" | sed 's/[[^.]][[^.]]*.\([[^.]][[^.]]*\).*/\1/'`
  ax_point_version=`echo "$2" | sed 's/[[^.]][[^.]]*.[[^.]][[^.]]*.\(.*\)/\1/'`

  AC_DEFINE_UNQUOTED([$1_MAJOR], [$ax_major_version], [Define to major version for $1])
  AC_DEFINE_UNQUOTED([$1_MINOR], [$ax_minor_version], [Define to minor version for $1])
  AC_DEFINE_UNQUOTED([$1_POINT], [$ax_point_version], [Define to point version for $1])
])
