      Making boxbackup run as a Windows Service

For most installations (with the default locations for config files,
etc.) running the install-cygwin-service.pl script will complete the
installation painlessly, and you will have a running bbackupd after
completing the installation, and whenever you reboot.

Simply run the script:

perl install-cygwin-service.pl

The service can be monitored in the Windows Service Manager. It is named
boxbackup.

For non-standard configurations, there are command-line options to point
the script to the bbackupd.conf config file, and the bbackupd.exe
executable:

perl install-cygwin-service.pl [-c <path-to-bbackupd-config-file>] [-e
<path-to-bbackupd-executable-file>]


      Removing the Service

If you decide not to run backups on a machine anymore, simply remove the
service by running:

sh remove-cygwin-service.sh


