#!/usr/bin/perl

# Usage: makedbcreate.pl inputfile output.cpp output.h

use strict;

# Older versions of postgres require sequences to be dropped explicitly.
# Set this to 1 to write code to drop these sequences for the postgresql driver.
my $old_versions_of_postgres_require_sequence_drop = 0;



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

# function base name
$input_file =~ m~([^/]+)\.[^\.]+\Z~;
my $fn_base_name = $1;

# open output files
open CPP,'>'.$output_cpp or die "Can't open $output_cpp for writing";

# write CPP boilerplate
$output_h =~ m~([^/]+)\Z~;
my $h_include = $1;
print CPP <<__E;
// automatically generated file, do not edit

#include "Box.h"

#include <string.h>

#include "$h_include"
#include "DatabaseConnection.h"
#include "DatabaseQueryGeneric.h"
#include "MemLeakFindOn.h"

void ${fn_base_name}_Create(DatabaseConnection &rConnection)
{
__E

# neaten up the input file, and split into queries
$input =~ s/(#|--).*$//mg;
my @to_drop;
my @postgresql_seq_to_drop;
for my $query (split /;/,$input)
{
	$query =~ s/\A\s+//;
	$query =~ s/\s+\Z//;
	next unless $query =~ m/\S/;
	
	if($query =~ m/CREATE\s+TABLE\s+(\w+)/si)
	{
		# make sure the table is dropped
		my $table_name = $1;
		push @to_drop,$table_name;
		# PostgreSQL needs sequences for serial numbers dropped too
		if($query =~ m/(\w+)\s+`AUTO_INCREMENT_INT/)
		{
			# need to drop this sequence too
			push @postgresql_seq_to_drop,$table_name.'_'.$1.'_seq';
		}
	}
	
	my $vendorise = ($query =~ m/`/)?'true':'false';
	
	# write a create statement for this
	my $sql;
	for my $line (split /[\r\n]+/,$query)
	{
		$line =~ s/\A\s+//;
		$line =~ s/\s+\Z//;
		next unless $line =~ m/\S/;
		$line =~ s/"/\\"/g;
		$sql .= " \"\\\n\t\t\t\"" unless $sql eq '';
		$sql .= $line;
	}

	print CPP <<__E;
	{
		DatabaseQueryGeneric create(rConnection, "$sql", $vendorise /* vendorise? */);
		create.Execute();
	}
__E
}

# and then drop
print CPP <<__E;
}

void ${fn_base_name}_Drop(DatabaseConnection &rConnection)
{
__E

for(@to_drop)
{
	print CPP <<__E;
	{
		DatabaseQueryGeneric drop(rConnection, "DROP TABLE $_");
		drop.Execute();
	}
__E
}
if($old_versions_of_postgres_require_sequence_drop)
{
	if($#postgresql_seq_to_drop >= 0)
	{
		# check for postgresql driver
		print CPP <<__E;
	if(::strcmp(rConnection.GetDriverName(), "postgresql") == 0)
	{
__E
	
		for(@postgresql_seq_to_drop)
		{
			print CPP <<__E;
		{
			DatabaseQueryGeneric drop(rConnection, "DROP SEQUENCE $_");
			drop.Execute();
		}
__E
		}
	
		print CPP <<__E;
	}
__E
	}
}

print CPP <<__E;
}


__E

close CPP;

# write H file
open H,'>'.$output_h or die "Can't open $output_h for writing";
my $guard = uc($output_h);
$guard =~ s/[^A-Z]/__/g;
print H <<__E;
// automatically generated file, do not edit

#ifndef $guard
#define $guard
class DatabaseConnection;

void ${fn_base_name}_Create(DatabaseConnection &rConnection);
void ${fn_base_name}_Drop(DatabaseConnection &rConnection);

#endif // $guard

__E


# finish up
close H;
