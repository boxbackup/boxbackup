#!/usr/bin/perl

use strict;
use lib 'infrastructure';
use BoxPlatform;

my $os_suffix = '';
if($build_os eq 'OpenBSD')
{
	$os_suffix = `uname -r`;
	$os_suffix =~ tr/0-9//cd;
}

my @parcels;
my %parcel_contents;

open PARCELS,"parcels.txt" or die "Can't open parcels file";
{
	my $cur_parcel = '';
	while(<PARCELS>)
	{
		chomp; s/#.+\Z//; s/\s+\Z//; s/\s+/ /g;
		next unless m/\S/;
		
		# omit bits on some platforms?
		next if m/\AEND-OMIT/;
		if(m/\AOMIT:(.+)/)
		{
			if($1 eq $build_os or $1 eq $target_os)
			{
				while(<PARCELS>)
				{
					last if m/\AEND-OMIT/;	
				}
			}
			next;
		}

		if (m'\AONLY:(.+)')
		{
			my @only_targets = split m'\,', $1;

			if (not grep {$_ eq $build_os or $_ eq $target_os}
				@only_targets)
			{
				while (<PARCELS>)
				{
					last if m'\AEND-ONLY';
				}
				next;
			}
		}
		
		# new parcel, or a new parcel definition?
		if(m/\A\s+(.+)\Z/)
		{
			push @{$parcel_contents{$cur_parcel}},$1
		}
		else
		{
			$cur_parcel = $_;
			push @parcels,$_;
		}
	}
}
close PARCELS;

# create parcels directory
mkdir "parcels",0755;
mkdir "parcels/scripts",0755;

# write master makefile

open MAKE,">Makefile" or die "Can't open master Makefile for writing";

print MAKE <<__E;
#
# AUTOMATICALLY GENERATED FILE
#    do not edit!
#
#

MAKE = $make_command

__E

print MAKE "all:\t",join(' ',map {parcel_target($_)} @parcels),"\n\n";

print MAKE "clean-parcels:\n";
for my $parcel (@parcels)
{
	print MAKE "\trm -rf ",parcel_dir($parcel),"\n";
	print MAKE "\trm -f ",parcel_target($parcel),"\n";
}

print MAKE "\n";
print MAKE "clean:\tclean-parcels\n";
print MAKE "\tfind . -name '*.o' | xargs -r rm\n";
print MAKE "\tfind . -name '*.a' | xargs -r rm\n";
print MAKE "\n";

print MAKE "test:\trelease/common/test\n\nrelease/common/test:\n\t./runtest.pl ALL release\n\n";

my $release_flag = BoxPlatform::make_flag('RELEASE');

for my $parcel (@parcels)
{
 	my @parcel_deps;
 
 	for (@{$parcel_contents{$parcel}})
 	{
 		my ($type,$name) = split /\s+/;
 		if($type eq 'bin')
 		{
 			push @parcel_deps, $name.$platform_exe_ext;
 			print MAKE "$name:\n" .
 				"\t(cd bin/$name; $make_command $release_flag)\n\n";
 		}
 		elsif ($type eq 'script')
 		{
 			push @parcel_deps, $name;
 		}
 	}
 
	my $target = parcel_target($parcel);
	print MAKE $target,": @parcel_deps\n";
  		
	my $dir = parcel_dir($parcel);
	print MAKE "\tmkdir -p $dir\n";
	
	open SCRIPT,">parcels/scripts/install-$parcel" 
		or die "Can't open installer script for $parcel for writing";

	print SCRIPT "#!/bin/sh\n\n";
	
	for(@{$parcel_contents{$parcel}})
	{
		my ($type,$name) = split /\s+/;
		
		if($type eq 'bin')
		{
			my $exeext = $platform_exe_ext;
			print MAKE "\t(cd bin/$name; \$(MAKE) $release_flag)\n";
			print MAKE "\tcp release/bin/$name/$name$exeext $dir\n";
			$name .= $exeext; # for install
		}
		elsif ($type eq 'script')
		{
			print MAKE "\tcp $name $dir\n";
		}

		# remove path from file name, for install
		$name =~ m~/([^/]+)\Z~;
		$name = $1;

		print SCRIPT "install $name ".
			"\$DESTDIR\${PREFIX:-$install_into_dir}\n";
	}
	
	close SCRIPT;
	
	chmod 0755,"parcels/scripts/install-$parcel";
	
	my $root = parcel_root($parcel);
	print MAKE "\tcp parcels/scripts/install-$parcel $dir\n";
	print MAKE "\t(cd parcels; tar cf - $root | gzip -9 - > $root.tgz )\n";
	
	print MAKE "\n";
	
	print MAKE "install-$parcel:\n";
	print MAKE "\t(cd $dir; ./install-$parcel \$(DESTDIR))\n\n";
}

print MAKE <<__E;
install:
	cat local/install.msg

__E

close MAKE;

open INSTALLMSG,">local/install.msg" or die "Can't open install message file for writing";
print INSTALLMSG <<__E;

Parcels need to be installed separately, and as root. Type one of the following:

__E

for(@parcels)
{
	print INSTALLMSG "    $make_command install-".$_."\n";
}
print INSTALLMSG "\n";

close INSTALLMSG;

sub parcel_root
{
	$product_name.'-'.$product_version.'-'.$_[0].'-'.$build_os.$os_suffix
}

sub parcel_dir
{
	'parcels/'.parcel_root($_[0])
}

sub parcel_target
{
	parcel_dir($_[0]).'.tgz'
}
