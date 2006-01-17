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

print "Creating built-in documentation for bbackupquery...\n";

open DOC,"documentation.txt" or die "Can't open documentation.txt file";
my $section;
my %help;
my @in_order;

while(<DOC>)
{
	if(m/\A>\s+(\w+)/)
	{
		$section = $1;
		m/\A>\s+(.+)\Z/;
		$help{$section} = $1."\n";
		push @in_order,$section;
	}
	elsif(m/\A</)
	{
		$section = '';
	}
	elsif($section ne '')
	{
		$help{$section} .= $_;
	}
}

close DOC;

open OUT,">autogen_Documentation.cpp" or die "Can't open output file for writing";

print OUT <<__E;
//
// Automatically generated file, do not edit.
//

#include "Box.h"

#include "MemLeakFindOn.h"

char *help_commands[] =
{
__E

for(@in_order)
{
	print OUT qq:\t"$_",\n:;
}

print OUT <<__E;
	0
};

char *help_text[] =
{
__E

for(@in_order)
{
	my $t = $help{$_};
	$t =~ s/\t/    /g;
	$t =~ s/\n/\\n/g;
	$t =~ s/"/\\"/g;
	print OUT qq:\t"$t",\n:;	
}

print OUT <<__E;
	0
};

__E

close OUT;
