#!/usr/bin/perl
# distribution boxbackup-0.09
# 
#  
# Copyright (c) 2003, 2004
#      Ben Summers.  All rights reserved.
#  
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All use of this software and associated advertising materials must 
#    display the following acknowledgement:
#        This product includes software developed by Ben Summers.
# 4. The names of the Authors may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
# 
# [Where legally impermissible the Authors do not disclaim liability for 
# direct physical injury or death caused solely by defects in the software 
# unless it is modified by a third party.]
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#  
#  
#  

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
			if($1 eq $build_os)
			{
				while(<PARCELS>)
				{
					last if m/\AEND-OMIT/;	
				}
			}
			next;
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

__E

print MAKE "all:\t",join(' ',map {parcel_target($_)} @parcels),"\n\n";

print MAKE "clean:\n";
for my $parcel (@parcels)
{
	print MAKE "\trm -rf ",parcel_dir($parcel),"\n";
	print MAKE "\trm -f ",parcel_target($parcel),"\n";
}
print MAKE "\n";

print MAKE "test:\trelease/common/test\n\nrelease/common/test:\n\t./runtest.pl ALL release\n\n";

my $release_flag = BoxPlatform::make_flag('RELEASE');

for my $parcel (@parcels)
{
	my $target = parcel_target($parcel);
	print MAKE $target,":\n";
	
	my $dir = parcel_dir($parcel);
	print MAKE "\tmkdir $dir\n";
	
	open SCRIPT,">parcels/scripts/install-$parcel" or die "Can't open installer script for $parcel for writing";
	print SCRIPT "#!/bin/sh\n\n";
	
	for(@{$parcel_contents{$parcel}})
	{
		my ($type,$name) = split /\s+/;
		
		if($type eq 'bin')
		{
			my $exeext = ($build_os eq 'CYGWIN')?'.exe':'';
			print MAKE "\t(cd bin/$name; $make_command $release_flag)\n";
			print MAKE "\tcp release/bin/$name/$name$exeext $dir\n";
		}
		elsif ($type eq 'script')
		{
			print MAKE "\tcp $name $dir\n";
			# remove path from script name
			$name =~ m~/([^/]+)\Z~;
			$name = $1;
		}

		print SCRIPT "install $name $install_into_dir\n";
	}
	
	close SCRIPT;
	
	chmod 0755,"parcels/scripts/install-$parcel";
	
	my $root = parcel_root($parcel);
	print MAKE "\tcp parcels/scripts/install-$parcel $dir\n";
	print MAKE "\t(cd parcels; tar cf - $root | gzip -9 - > $root.tgz )\n";
	
	print MAKE "\n";
	
	print MAKE "install-$parcel:\n";
	print MAKE "\t(cd $dir; ./install-$parcel)\n\n";
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
	print INSTALLMSG "    make install-".$_."\n";
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
