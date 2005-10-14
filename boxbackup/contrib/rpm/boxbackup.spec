%define bb_user_id 171
%define ident %{name}-%{version}

# Detect distribution. So far we only special-case SUSE. If you need to make
# any distro specific changes to get the package building on your system
# please email them to martin@zepler.org
#%define is_fc   %(test -e %{_sysconfdir}/fedora-release && echo 1 || echo 0)
#%define is_mdk  %(test -e %{_sysconfdir}/mandrake-release && echo 1 || echo 0)
#%define is_rh   %(test -e %{_sysconfdir}/redhat-release && echo 1 || echo 0)
%define is_suse %(test -e %{_sysconfdir}/SuSE-release && echo 1 || echo 0)

%if %{is_suse}
%define init_dir %{_sysconfdir}/init.d
%define dist suse
%define rc_start rc
%else
%define init_dir %{_sysconfdir}/rc.d/init.d
%define dist redhat
%define rc_start "service "
%endif

Summary: An automatic on-line backup system for UNIX.
Name: boxbackup
Version: ###DISTRIBUTION-VERSION-NUMBER###
Release: 1
License: BSD
Group: Applications/Archiving
Packager: Martin Ebourne <martin@zepler.org>
URL: http://www.fluffy.co.uk/boxbackup/
Source0: %{ident}.tgz
Requires: openssl >= 0.9.7a
BuildRoot: %{_tmppath}/%{ident}-%{release}-root
BuildRequires: openssl >= 0.9.7a, openssl-devel

%description
Box Backup is a completely automatic on-line backup system. Backed up files
are stored encrypted on a filesystem on a remote server, which does not need
to be trusted. The backup server runs as a daemon on the client copying only
the changes within files, and old versions and deleted files are retained. It
is designed to be easy and cheap to run a server and (optional) RAID is
implemented in userland for ease of use.

%package client
Summary: An automatic on-line backup system for UNIX.
Group: Applications/Archiving

%description client
Box Backup is a completely automatic on-line backup system. Backed up files
are stored encrypted on a filesystem on a remote server, which does not need
to be trusted. The backup server runs as a daemon on the client copying only
the changes within files, and old versions and deleted files are retained. It
is designed to be easy and cheap to run a server and (optional) RAID is
implemented in userland for ease of use.

This package contains the client.

%package server
Summary: An automatic on-line backup system for UNIX.
Group: System Environment/Daemons

%description server
Box Backup is a completely automatic on-line backup system. Backed up files
are stored encrypted on a filesystem on a remote server, which does not need
to be trusted. The backup server runs as a daemon on the client copying only
the changes within files, and old versions and deleted files are retained. It
is designed to be easy and cheap to run a server and (optional) RAID is
implemented in userland for ease of use.

This package contains the server.

%prep
%setup -q

%build
./configure

make

%install
rm -rf $RPM_BUILD_ROOT

mkdir -p $RPM_BUILD_ROOT%{_docdir}/%{ident}
mkdir -p $RPM_BUILD_ROOT%{_bindir}
mkdir -p $RPM_BUILD_ROOT%{_sbindir}
mkdir -p $RPM_BUILD_ROOT%{init_dir}
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/box/bbackupd
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/box/bbstored
mkdir -p $RPM_BUILD_ROOT%{_var}/lib/box

install -m 644 BUGS.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 LINUX.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 VERSION.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 CONTACT.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 DOCUMENTATION.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 ExceptionCodes.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 THANKS.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 LICENSE.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}
install -m 644 TODO.txt $RPM_BUILD_ROOT%{_docdir}/%{ident}

# Client
touch $RPM_BUILD_ROOT%{_sysconfdir}/box/bbackupd.conf
install -m 755 contrib/%{dist}/bbackupd $RPM_BUILD_ROOT%{init_dir}
%if %{is_suse}
ln -s ../../%{init_dir}/bbackupd $RPM_BUILD_ROOT%{_sbindir}/rcbbackupd
%endif
%define client_dir parcels/%{ident}-backup-client-Linux
install %{client_dir}/bbackupd $RPM_BUILD_ROOT%{_sbindir}
install %{client_dir}/bbackupquery $RPM_BUILD_ROOT%{_sbindir}
install %{client_dir}/bbackupctl $RPM_BUILD_ROOT%{_sbindir}
install %{client_dir}/bbackupd-config $RPM_BUILD_ROOT%{_sbindir}

# Server
touch $RPM_BUILD_ROOT%{_sysconfdir}/box/bbstored.conf
touch $RPM_BUILD_ROOT%{_sysconfdir}/box/raidfile.conf
install -m 755 contrib/%{dist}/bbstored $RPM_BUILD_ROOT%{init_dir}
%if %{is_suse}
ln -s ../../%{init_dir}/bbstored $RPM_BUILD_ROOT%{_sbindir}/rcbbstored
%endif
%define server_dir parcels/%{ident}-backup-server-Linux
install %{server_dir}/bbstored $RPM_BUILD_ROOT%{_sbindir}
install %{server_dir}/bbstoreaccounts $RPM_BUILD_ROOT%{_sbindir}
install %{server_dir}/bbstored-certs $RPM_BUILD_ROOT%{_bindir}
install %{server_dir}/bbstored-config $RPM_BUILD_ROOT%{_sbindir}
install %{server_dir}/raidfile-config $RPM_BUILD_ROOT%{_sbindir}

%pre server
%{_sbindir}/useradd -c "Box Backup" -u %{bb_user_id} \
	-s /sbin/nologin -r -d / box 2> /dev/null || :

%post client
/sbin/chkconfig --add bbackupd
if [ ! -f %{_sysconfdir}/box/bbackupd.conf ]; then
	echo "You should run the following to configure the client:"
	echo "bbackupd-config %{_sysconfdir}/box lazy <account-number> <server-host>" \
	     "%{_var}/lib/box <backup-directories>"
	echo "Then follow the instructions. Use this to start the client:"
	echo "%{rc_start}bbackupd start"
fi

%post server
/sbin/chkconfig --add bbstored
if [ ! -f %{_sysconfdir}/box/bbstored.conf ]; then
	echo "You should run the following to configure the server:"
	echo "raidfile-config %{_sysconfdir}/box 2048 <store-directory> [<raid-directories>]"
	echo "bbstored-config %{_sysconfdir}/box" `hostname` box
	echo "Then follow the instructions. Use this to start the server:"
	echo "%{rc_start}bbstored start"
fi

%preun client
if [ $1 = 0 ]; then
	%{init_dir}/bbackupd stop > /dev/null 2>&1
	/sbin/chkconfig --del bbackupd
fi

%preun server
if [ $1 = 0 ]; then
	%{init_dir}/bbstored stop > /dev/null 2>&1
	/sbin/chkconfig --del bbstored
fi


%clean
rm -rf $RPM_BUILD_ROOT

%files client
%defattr(-,root,root,-)
%dir %attr(700,root,root) %{_sysconfdir}/box/bbackupd
%dir %attr(755,root,root) %{_var}/lib/box
%doc %{_docdir}/%{ident}/*.txt
%config %{init_dir}/bbackupd
%if %{is_suse}
%{_sbindir}/rcbbackupd
%endif
%config %ghost %{_sysconfdir}/box/bbackupd.conf
%{_sbindir}/bbackupd
%{_sbindir}/bbackupquery
%{_sbindir}/bbackupctl
%{_sbindir}/bbackupd-config

%files server
%defattr(-,root,root,-)
%dir %attr(700,box,root) %{_sysconfdir}/box/bbstored
%config %{init_dir}/bbstored
%if %{is_suse}
%{_sbindir}/rcbbstored
%endif
%config %ghost %{_sysconfdir}/box/bbstored.conf
%config %ghost %{_sysconfdir}/box/raidfile.conf
%{_sbindir}/bbstored
%{_sbindir}/bbstoreaccounts
%{_bindir}/bbstored-certs
%{_sbindir}/bbstored-config
%{_sbindir}/raidfile-config

%changelog
* Fri Oct  1 2004 Martin Ebourne <martin@zepler.org> - 0.08-3
- Moved most of the exes to /usr/sbin
- SUSE updates from Chris Smith

* Fri Sep 24 2004 Martin Ebourne <martin@zepler.org> - 0.08-2
- Added support for other distros
- Changes for SUSE provided by Chris Smith <chris.smith@nothingbutnet.co.nz>

* Mon Sep 16 2004 Martin Ebourne <martin@zepler.org> - 0.07-1
- Initial build
