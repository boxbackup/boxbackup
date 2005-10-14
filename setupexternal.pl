#!/usr/bin/perl
use strict;

# This script links in the essential directories and processes various
# files to allow the Box libraries to be used in projects outside the main
# box library tree.

# directories to link through
my @linkdirs = qw/lib infrastructure/;

# ----------------------------------------------------

my $libdir = $ARGV[0];
die "Provided library dir $libdir does not exist" unless -d $libdir;

# Check and remove links from the directory, then add new symlinks
for my $d (@linkdirs)
{
	if(-e $d)
	{
		die "In project, $d is not a symbolic link"
			unless -l $d;
		print "Removing existing symlink $d\n";
		unlink $d;
	}
	my $link_target = "$libdir/$d";
	print "Add symlink $d -> $link_target\n";
	die "Can't create symlink $d" unless
		symlink $link_target, $d;
}

# Copy and create a base modules file which includes all the libraries
print "Create new modules_base.txt file\n";
open OUT,">modules_base.txt" or die "Can't open modules_base.txt file for writing";
print OUT <<__E;
#
# Automatically generated file, do not edit
#
# Source: $libdir/modules.txt
#

__E

open IN,"$libdir/modules.txt" or die "Can't open $libdir/modules.txt for reading";

while(<IN>)
{
	if(m/\A(lib\/.+?)\s/)
	{
		print OUT
	}
}

close IN;
close OUT;
