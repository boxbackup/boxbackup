dnl @synopsis AX_CHECK_MOUNT_POINT([ACTION-IF-TRUE], [ACTION-IF-FALSE])
dnl
dnl This macro will find out how to get mount point information if possible.
dnl
dnl The following defines will be set as appropriate:
dnl HAVE_MOUNTS
dnl HAVE_MNTENT_H
dnl HAVE_SYS_MNTTAB_H
dnl HAVE_SYS_MOUNT_H
dnl HAVE_STRUCT_MNTENT_MNT_DIR
dnl HAVE_STRUCT_MNTTAB_MNT_MOUNTP
dnl HAVE_STRUCT_STATFS_F_MNTONNAME
dnl HAVE_STRUCT_STATVFS_F_MNTONNAME
dnl Also ACTION-IF-TRUE and ACTION-IF-FALSE are run as appropriate
dnl
dnl @category C
dnl @author Martin Ebourne
dnl @version 2005/07/01
dnl @license AllPermissive

AC_DEFUN([AX_CHECK_MOUNT_POINT], [
  AC_CHECK_FUNCS([getmntent statfs])
  AC_CHECK_HEADERS([sys/param.h])
  AC_CHECK_HEADERS([mntent.h sys/mnttab.h sys/mount.h],,, [[
    #include <stdio.h>
    #ifdef HAVE_SYS_PARAM_H
      #include <sys/param.h>
    #endif
    ]])
  # BSD
  AC_CHECK_MEMBERS([struct statfs.f_mntonname],,, [[
    #ifdef HAVE_SYS_PARAM_H
      #include <sys/param.h>
    #endif
    #include <sys/mount.h>
    ]])
  # NetBSD
  AC_CHECK_MEMBERS([struct statvfs.f_mntonname],,, [[
    #ifdef HAVE_SYS_PARAM_H
      #include <sys/param.h>
    #endif
    #include <sys/mount.h>
    ]])
  # Linux
  AC_CHECK_MEMBERS([struct mntent.mnt_dir],,, [[#include <mntent.h>]])
  # Solaris
  AC_CHECK_MEMBERS([struct mnttab.mnt_mountp],,, [[
    #include <stdio.h>
    #include <sys/mnttab.h>
    ]])
  if test "x$ac_cv_member_struct_statfs_f_mntonname" = "xyes" || \
     test "x$ac_cv_member_struct_statvfs_f_mntonname" = "xyes" || \
     test "x$ac_cv_member_struct_mntent_mnt_dir" = "xyes" || \
     test "x$ac_cv_member_struct_mnttab_mnt_mountp" = "xyes"
  then
    AC_DEFINE([HAVE_MOUNTS], [1], [Define to 1 if this platform supports mounts])
    m4_ifvaln([$1],[$1],[:])dnl
    m4_ifvaln([$2],[else $2])dnl
  fi
  ])dnl
