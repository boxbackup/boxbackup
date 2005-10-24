#!/usr/bin/perl
use strict;

my @del_macos_files;
my @bad_cpp;
my @test_main;
my @makefiles;
my @autogen_cpp;
my $cleaned = 1;
my $dist_archives_exist = 0;
my @bad_h;

open EVERYTHING,'find . |' or die "Can't open find for file listing";

my %exclude_from_memtest_checks = ('PollEmulator.cpp'=>1,'DebugMemLeakFinder.cpp'=>1,'MemLeakFinder.h'=>1,'MemLeakFindOn.h'=>1,'MemLeakFindOff.h'=>1,'Box.h'=>1);

while(<EVERYTHING>)
{
	chomp;
	next if -d;
	if(m~/autogen_\w+\.(h|cpp)~)	
	{
		push @autogen_cpp,$_
	}
	if(m~/\._[^/]+\Z~ || m~/\.DS_Store\Z~)
	{
		# mac OS files we don't want
		push @del_macos_files,$_
	}
	elsif(m/\/(\w+\.cpp)/)
	{
		my $leafname = $1;
		# check that Box.h is first include
		open CPP,$_ or die "Can't open $_ for reading";
		
		my $box_found = 0;
		my $last_was_memteston = 0;
		my $ok = 1;
		
		while(my $l = <CPP>)
		{
			if($l =~ m/#include\s+["<](.+?)[">]/)
			{
				my $inc_name = $1;
				if($inc_name eq 'Box.h')
				{
					$box_found = 1;
				}
				else
				{
					# Box.h must be first include file in every cpp file
					$ok = 0 unless $box_found;
				}
				# is it the mem test on thing? (ignoring the wire packing .h files)
				if($inc_name ne 'BeginStructPackForWire.h' && $inc_name ne 'EndStructPackForWire.h')
				{
					$last_was_memteston = ($inc_name eq 'MemLeakFindOn.h');
				}
			}
		}
		if(!exists $exclude_from_memtest_checks{$leafname})
		{
			$ok = 0 unless $last_was_memteston;
		}
		push @bad_cpp,$_ unless $ok;
				
		close CPP;
	}
	elsif(m/\/(\w+\.h)/)
	{
		my $leafname = $1;

		open H,$_ or die "Can't open $_ for reading";
		
		my $ok = 1;
		my $memteston = 0;
		
		while(my $l = <H>)
		{
			if($l =~ m/#include\s+["<](.+?)[">]/)
			{
				if($1 eq 'MemLeakFindOn.h')
				{
					$memteston = 1;
				}
				elsif($1 eq 'MemLeakFindOff.h')
				{
					$memteston = 0;
				}
				else
				{
					# don't allow #include within mem test on
					$ok = 0 unless !$memteston;
				}
			}
			else
			{
				# strip comments
				my $lsc = $l;
				$lsc =~ s~//.+$~~;
				if($lsc =~ m/\b(new|delete|malloc|free|realloc)\b/)
				{
					# only allow this if memory checking is ON
					$ok = 0 unless $memteston;
				}
			}
		}
		# mem test must be off at the end of this .h file
		$ok = 0 if $memteston;
		
		if($_ !~ /testfiles/ && !exists $exclude_from_memtest_checks{$leafname})
		{
			push @bad_h,$_ unless $ok;
		}
		close H;
	}
	elsif(m~/Makefile\Z~)
	{
		push @makefiles,$_
	}
	
	if(m~/_(main\.cpp|t|t-gdb)\Z~)
	{
		push @test_main,$_
	}
	
	if(m~\./boxbackup~)
	{
		$dist_archives_exist = 1;
	}
}

close EVERYTHING;

ask_about_delete(\@del_macos_files, "supurious MacOS X files");
ask_about_delete(\@makefiles, "automatically generated Makefiles");
ask_about_delete(\@test_main, "automatically generated test files");
ask_about_delete(\@autogen_cpp, "automatically generated source files");

if($#bad_cpp >= 0)
{
	print "\n";
	print $_,"\n" for @bad_cpp;
	print "There are some .cpp file where Box.h is not the first included file or MemLeakFindOn.h is not the last .h file included\n";
	$cleaned = 0;
}
if($#bad_h >= 0)
{
	print "\n";
	print $_,"\n" for @bad_h;
	print "There are some .h files which use memory functions without memory leak finding on, or leave memory leak finding on at end\n";
	$cleaned = 0;
}

if(-d 'debug') {print "debug directory exists\n"; $cleaned = 0;}
if(-d 'release') {print "release directory exists\n"; $cleaned = 0;}
if(-d 'parcels') {print "parcels directory exists\n"; $cleaned = 0;}
if($dist_archives_exist) {print "boxbackup* files/dirs exist\n"; $cleaned = 0;}

if(!$cleaned)
{
	print <<__E;

========================================================
   NOT CLEANED!
========================================================
__E
}


sub ask_about_delete
{
	my ($del_r, $name) = @_;
	return if $#$del_r < 0;
		
	print "\n";
	for(@$del_r)
	{
		print $_,"\n";
	}
	print "Delete these ",$#$del_r + 1, " $name?";
	my $in = <STDIN>;
	chomp $in;
	if($in eq 'yes')
	{
		print "Deleting...\n";
		unlink $_ for @$del_r
	}
	else
	{
		$cleaned = 0;
	}
}



