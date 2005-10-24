#!/usr/bin/perl
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
