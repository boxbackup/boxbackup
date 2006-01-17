#!/usr/bin/perl -w


# Contributed to the boxbackup project by Per Reedtz Thomsen. pthomsen@reedtz.com

# This script reads the config file for boxbackup, and changes the mode
# of the directory named by 'DataDirectory' and any files there. Also,
# the files pointed to by the 'CommandSocket' and 'PidFile' configuration
# parameters will be chmod'ed to be read-write by all.
# The Windows services are created and started using the 'cygrunsrv' utility.

# Date      Who                      Comments
# 20041005  pthomsen@reedtz.com      Created 
# 20041020  pthomsen@reedtz.com	     Switched to using Getopt::Std for cmd-line things.

use strict;

use Getopt::Std;
getopt('ce');
our ($opt_c, $opt_e);
# Figure out the config file to use. Default is /etc/box/bbackupd.conf
my $confFile = (defined($opt_c) ? $opt_c :  "/etc/box/bbackupd.conf");
# Figure out the bbaackupd executable to use. Default is /usr/local/bin/bbackupd.exe
my $exeFile = (defined($opt_e) ? $opt_e :  "/usr/local/bin/bbackupd.exe");

die "File $confFile does not exist. Please provide the full path to the bbackupd configuration file.\n" if !(-f $confFile);
die "Can't read $confFile. Permission denied. Please chmod the file so I can read it.\n" if !(-r $confFile);
die "File $exeFile does not exist. Please provide the full path to the bbackupd.exe file.\n" if !(-f $exeFile);
die "File $exeFile is not executable. Please provide the full path to the correct bbackupd.exe file.\n" if !(-x $exeFile);

# print "Config: $confFile\n";

my $dataDir;
my $cmdSocket;
my $pidFile;

open (CONFIG, "<$confFile") or die "Can't open $confFile: $!\n";

# Read the confgiguration file, and pull the DataDirectory, CommandSocket, and PidFile parameters.
while (<CONFIG>)
{
    
    if (/^\s*DataDirectory\s*=\s*([^\n\s]+)\s*\n/)
    {
	$dataDir = $1;
	next;
    }

    if (/^\s*CommandSocket\s*=\s*([^\n\s]+)\s*\n/)
    {
	$cmdSocket = $1;
	next;
    }
    if (/^\s*PidFile\s*=\s*([^\n\s]+)\s*\n/)
    {
	$pidFile = $1;
	next;
    }
}

# check that we got all the parameters from the file. If not, die.
if ((!defined($dataDir)) || (!defined($cmdSocket)) || (!defined($pidFile)))
{
    die "Could not read config parameters from $confFile. Values retrieved:\n\tDataDirectory = $dataDir\n\tCommandSocket = $cmdSocket\n\tPidFile = $pidFile\n";
}


print  "Parameters retrieved from $confFile. Values:\n\tDataDirectory = $dataDir\n\tCommandSocket = $cmdSocket\n\tPidFile = $pidFile\n";
print "chmod...";
# change the mode of the files/dirs retrieved.
chmod(0777, $dataDir) or die "Can't chmod $dataDir: $!\n";
chmod(0666, "$dataDir/*") or die "Can't chmod $dataDir/*: $!\n";
chmod(0666, $pidFile) or die "Can't chmod $pidFile: $!\n";
chmod(0755, $cmdSocket) or die "Can't chmod $cmdSocket: $!\n";
print " Done.\n";

# Now install the service using cygrunsrv.
# Details: 
#   -I <svc_name>      Install a service. svc_name is the name under which the
#                      service will appear in the Windows Service Manager
#   -p <path_to_exe>   Path to the executable.
#   -a <options>       Command line options to the executable.
#   -f <description>   Description of the service.
#   -o                 Attempt clean exit of service during system shutdown

print "Installing boxbackup service...";
my $sysCmd = "cygrunsrv.exe -I boxbackup -p " . $exeFile;
$sysCmd .= " -a \"" . $confFile . " SINGLEPROCESS\"";
$sysCmd .= " -o -f \"Online Backup System by Ben Summers\"";
print "$sysCmd\n";
my $output = qx($sysCmd);
die "cygrunsrv failed to install service. Error Message: $output\n" if($output ne "");
print " Done.\n";


# Start the service
# Details:
#   -S <svc_name>     Start a service. svc_name is the name of the (installed)
#                     service to start.

print "Starting boxbackup service...";
$sysCmd = "cygrunsrv.exe -S boxbackup";
print "$sysCmd\n";
$output = qx($sysCmd);
die "cygrunsrv failed to start service. Error Message: $output\n" if($output ne "");
print " Done.\n";

print "\n\nService Installation complete. To test, reboot your machine, and make sure that\n";
print "the boxbackup service is running. A good way to make sure, is to check that the account number\n";
print "from this machine is connecting to the bbstored server. Check the bbstored logs for more info.\n\n";


