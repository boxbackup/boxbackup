#!/usr/bin/perl

# Usage: makedbmake.pl directory

use strict;

my ($dir) = @ARGV;
unless(-d $dir)
{
	die "Directory $dir not found"
}

print "Making makefile for database autogen in $dir\n";

# make output directory
my $output_dir = "$dir/autogen_db";
unless(-d $output_dir)
{
	mkdir $output_dir,0755
		or die "Can't create directory $output_dir"
}

# go through entries in this file, writing database output lines
opendir DIR,$dir or die "Can't open $dir for reading";
my @items = readdir DIR;
close DIR;

open MAKE,">$dir/Makefile.db" or die "Can't open makefile for writing";

for my $item (@items)
{
	next unless $item =~ m/\.(schema|query|cpp)\Z/;
	
	my $input_filename = "$dir/$item";
	my $output_h = '';
	my $output_cpp = '';
	
	if($item =~ m/\A(.+?)\.schema\Z/)
	{
		my $o_cpp = "autogen_db/$1_schema.cpp";
		my $o_h = "autogen_db/$1_schema.h";
		my $cmd = '../../lib/database/makedbcreate.pl '.$item.' '.$o_cpp.' '.$o_h;
		print MAKE "$o_cpp $o_h:\t$item\n\t$cmd\n\n";
		die "Running command $cmd failed" unless system("(cd $dir; $cmd)") == 0;
	}
	elsif($item =~ m/\A(.+?)\.query\Z/)
	{
		# query file, easy
		$output_h = "autogen_db/$1.h";
		$output_cpp = "autogen_db/$1.cpp";
	}
	elsif($item =~ m/\A(.+?)\.cpp\Z/)
	{
		# not quite so easy, have to see if it contains any queries
		my $fr = $1;
		open FL,$input_filename or die "Can't open $input_filename for reading";
		my $c;
		read FL,$c,-s $input_filename;
		close FL;
		if($c =~ /\r?\n?\s*SQL\s+Query\s*[\r\n]+\s*\[\s*[\r\n]+.+?[\r\n]+\s*\]\s*[\r\n]/sg)
		{
			# yes...
			$output_h = "autogen_db/${fr}_query.h";
			$output_cpp = "autogen_db/${fr}_query.cpp";
		}
	}
	
	# if output is necessary, generate the makefile line and run the program
	if($output_h ne '')
	{
		my $cmd = '../../lib/database/makequeries.pl '.$item.' '.$output_cpp.' '.$output_h;
		print MAKE "$output_cpp $output_h:\t$item\n\t$cmd\n\n";
		die "Running command $cmd failed" unless system("(cd $dir; $cmd)") == 0;
	}
}

close MAKE;

