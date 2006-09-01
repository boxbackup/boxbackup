#!/usr/bin/perl
use strict;

my $in = $ARGV[0];
if($in eq '' || !-f $in)
{
	die "No input file, or file not found"
}

open IN,$in or die "can't open input";
open OUT,">PostgreSQLOidTypes.h" or die "can't open output";

print OUT <<__E;
// Automatically generated file, do not edit.
//
// Used because including the catalog/pg_type.h file is a bit complicated
// because of various assumptions used by packagers on some platforms.
//

#ifndef POSTGRESQLOIDTYPES__H
#define POSTGRESQLOIDTYPES__H

__E

while(<IN>)
{
	print OUT if m/#define.+OID.+?\d+/;
}

print OUT <<__E;

#endif // POSTGRESQLOIDTYPES__H
__E

close OUT;
close IN;

