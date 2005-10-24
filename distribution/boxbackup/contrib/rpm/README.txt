BUILDING AN RPM

The easy way is to:

rpmbuild -ta <tarfile>

where <tarfile> is the archive you downloaded of Box Backup.

This RPM should work on RedHat Enterprise, Fedora Core, Mandrake, and any
similar distributions. It has been developed and tested on Fedora Core.

SUSE Linux will need edits to the init scripts (bbackupd and bbstored), and
may need changes to the spec files too. I don't run SUSE so can't be sure.

Martin Ebourne
martin@zepler.org
