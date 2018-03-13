#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([Box Backup], 0.11, [boxbackup@boxbackup.org],[boxbackup])
AC_CONFIG_SRCDIR([lib/common/Box.h])
AC_CONFIG_AUX_DIR([infrastructure])
AC_CONFIG_HEADERS([lib/common/BoxConfig.h])
AC_CONFIG_SUBDIRS([qdbm])

touch install-sh
AC_CANONICAL_SYSTEM
test -s install-sh || rm install-sh

### Checks for programs.

AC_LANG([C++])
AC_PROG_CC
AC_PROG_CXX
AC_CXX_EXCEPTIONS
AC_CXX_NAMESPACES
if test "x$ac_cv_cxx_exceptions" != "xyes" || \
   test "x$ac_cv_cxx_namespaces" != "xyes"; then
  AC_MSG_ERROR([[basic compile checks failed, the C++ compiler is broken]])
fi

m4_include([infrastructure/m4/boxbackup_tests.m4])

## Get tmpdir
temp_directory_name="/tmp"
AC_ARG_WITH(
  [tmp-dir],
  [AC_HELP_STRING([--with-tmp-dir=DIR], [Directory for temporary files [/tmp]])],
  [temp_directory_name="$withval"])
AC_DEFINE_UNQUOTED([TEMP_DIRECTORY_NAME], ["$temp_directory_name"], [TMP directory name])

## Allow linking binaries with static libraries
AC_ARG_ENABLE(
  [static-bin],
  [AC_HELP_STRING([--enable-static-bin], [Link binaries with static libraries])])
if test "x$enable_static_bin" = "xyes"; then
  AC_CHECK_LIB([ssl],[SSL_read],,, [crypto])
  LIBS="-Wl,-Bstatic $LIBS -Wl,-Bdynamic"
fi

# override default sysconfdir, for backwards compatibility
test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc
test "$localstatedir" = '${prefix}/var' && localstatedir=/var

## Kludge to allow makeparcels.pl to use bindir. This is not a good long term
## solution because it prevents use of "make exec_prefix=/some/dir"
saved_prefix=$prefix
saved_exec_prefix=$exec_prefix
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
eval bindir_expanded=`       eval "echo $bindir"`
eval sbindir_expanded=`      eval "echo $sbindir"`
eval sysconfdir_expanded=`   eval "echo $sysconfdir"`
eval localstatedir_expanded=`eval "echo $localstatedir"`
prefix=$saved_prefix
exec_prefix=$saved_exec_prefix
AC_SUBST([bindir_expanded])
AC_SUBST([sbindir_expanded])
AC_SUBST([sysconfdir_expanded])
AC_SUBST([localstatedir_expanded])

## Figure out the client parcel directory and substitute it
build_dir=`dirname $0`
build_dir=`cd $build_dir && pwd`
client_parcel_dir=`$PERL infrastructure/parcelpath.pl backup-client $target_os`

if test "$build_os" = "cygwin"; then
	client_parcel_dir=`cygpath -wa $client_parcel_dir | sed -e 's|\\\|/|g'`
	build_dir=`        cygpath -wa $build_dir         | sed -e 's|\\\|/|g'`
fi

AC_SUBST([client_parcel_dir])
AC_SUBST([build_dir])

## Figure out version and substitute it in
box_version=`$PERL infrastructure/printversion.pl`
AC_SUBST([box_version])

### Output files
AC_CONFIG_FILES([infrastructure/BoxPlatform.pm
                 contrib/mac_osx/org.boxbackup.bbackupd.plist
                 contrib/mac_osx/org.boxbackup.bbstored.plist
                 contrib/solaris/bbackupd-manifest.xml
                 contrib/solaris/bbstored-manifest.xml
                 lib/common/BoxPortsAndFiles.h
                 test/bbackupd/testfiles/bbackupd.conf
                 test/bbackupd/testfiles/bbackupd-exclude.conf
                 test/bbackupd/testfiles/bbackupd-snapshot.conf
                 test/bbackupd/testfiles/bbackupd-symlink.conf
                 test/bbackupd/testfiles/bbackupd-temploc.conf
  		 ])
AX_CONFIG_SCRIPTS([bin/bbackupd/bbackupd-config
                   bin/bbstored/bbstored-certs
                   bin/bbstored/bbstored-config
                   contrib/debian/bbackupd
                   contrib/debian/bbstored
                   contrib/redhat/bbackupd
                   contrib/redhat/bbstored
                   contrib/suse/bbackupd
                   contrib/suse/bbstored
                   contrib/solaris/bbackupd-smf-method
                   contrib/solaris/bbstored-smf-method
                   contrib/windows/installer/boxbackup.mpi
                   infrastructure/makebuildenv.pl
                   infrastructure/makedistribution.pl
                   lib/bbackupquery/makedocumentation.pl
                   lib/common/makeexception.pl
                   lib/raidfile/raidfile-config
                   lib/server/makeprotocol.pl
                   runtest.pl
                   test/backupstorefix/testfiles/testbackupstorefix.pl
                   test/bbackupd/testfiles/extcheck1.pl
                   test/bbackupd/testfiles/extcheck2.pl
                   test/bbackupd/testfiles/notifyscript.pl
                   test/bbackupd/testfiles/syncallowscript.pl])
# TODO: Need to do contrib/cygwin/install-cygwin-service.pl but location varies
AC_OUTPUT

# Configure the Box build system
echo
if ! $PERL ./infrastructure/makebuildenv.pl; then
	echo "Making infrastructure failed!"
	exit 1
fi

# Write summary of important info
tee config.log.features <<EOC
A summary of the build configuration is below. Box Backup will function
without these features, but will work better where they are present. Refer
to the documentation for more information on each feature.

Regular expressions: $have_regex_support
Large files:         $box_cv_have_large_file_support
Berkeley DB:         $ax_path_bdb_ok
Readline:            $have_libreadline
Extended attributes: $ac_cv_header_sys_xattr_h
Debugger:            ${with_debugger:-neither GDB nor LLDB detected!}
EOC

cat > config.env <<EOC
CC="$CC"
CXX="$CXX"
CXXFLAGS="$CXXFLAGS"
LDFLAGS="$LDFLAGS"
LIBS="$LIBS"
EOC

### Warnings at end for visibility

if test "x$box_cv_gcc_3_plus" != "xyes" && test "x$box_cv_malloc_workaround" != "xyes"; then
  echo
  AC_MSG_WARN([[the implementation of the C++ STL on this platform may
have a flaw which causes it to apparently leak memory, and this flaw cannot be
worked around.

When running the daemons, check their memory usage does not constantly
increase. The STL flaw can cause excessive memory use.]])
fi

case "$vl_cv_lib_readline" in
  *readline*)
    echo
    AC_MSG_WARN([[do not distribute binaries compiled against GNU readline,
as this could violate the GNU readline licence, which is GPL. You may use
libedit or libeditline instead.]])
    ;;
esac
