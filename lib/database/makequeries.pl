#!/usr/bin/perl

# Usage: makequeries.pl inputfile output.cpp output.h

use strict;
use lib '../../lib/perl';
use lib '../../lib/database';
use Database::Query;

my ($input_file, $output_cpp, $output_h) = @ARGV;
if(!-f $input_file)
{
	die "Input file $input_file not found";
}
if($output_cpp eq '' || $output_h eq '')
{
	die "Output files not specified";
}

# read in input
open IN,$input_file or die "Can't open $input_file";
my $input;
read IN,$input,-s $input_file;
close IN;

# open output files
open CPP,'>'.$output_cpp or die "Can't open $output_cpp for writing";
open H,'>'.$output_h or die "Can't open $output_h for writing";

# write boilerplate
$output_h =~ m~([^/]+)\Z~;
my $h_include = $1;
print CPP <<__E;
// automatically generated file, do not edit

#include "Box.h"
#include "$h_include"
#include "DatabaseConnection.h"
#include "MemLeakFindOn.h"
__E
my $guard = uc($output_h);
$guard =~ s/[^A-Z]/__/g;
print H <<__E;
// automatically generated file, do not edit

#ifndef $guard
#define $guard
#include "DatabaseQuery.h"
__E

# some queries will need DatabaseQueryGeneric.h included
my $generic_header_included = 0;

# go through input file, finding queries and writing things out
while($input =~ /\r?\n?\s*SQL\s+Query\s*[\r\n]+\s*\[\s*[\r\n]+(.+?)[\r\n]+\s*\]\s*[\r\n]/sg)
{
	my $defn = $1;
	#print $defn,"\n";
	my %attr;
	my $cattr = '';
	for(split /[\r\n]+/,$defn)
	{
		s/\A\s+//; s/\s+\Z//;
		if(m/\A(\w+):\s*(.+)\Z/)
		{
			$cattr = $1;
			die "Attribute $cattr already defined in query" if exists $attr{$cattr};
			$attr{$cattr} = $2;
		}
		else
		{
			if($cattr ne '')
			{
				$attr{$cattr} .= ' '.$_
			}
			else
			{
				die "First line of definition doesn't have an attribute name"
			}
		}
	}
	
	# build a query object
	my $query = Database::Query->new(%attr);
	
	# need to include the header file?
	if($query->dervied_from_DatabaseQueryGeneric())
	{
		unless($generic_header_included)
		{
			$generic_header_included = 1;
			print H '#include "DatabaseQueryGeneric.h"',"\n";
		}
	}
	
	# write the object out to the files
	print CPP $query->generate_cpp();
	print H $query->generate_h();
}

# finish up
print H <<__E;
#endif // $guard

__E
close H;
close CPP;
