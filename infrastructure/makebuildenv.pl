#!/usr/bin/perl
use strict;
use Symbol;

my @modules;
my %module_dependency;
my %module_library_link_opts;
my %header_dependency;

$|=1;


# note: Mac OS X resource forks and .DS_Store files are explicity ignored

print "Box build environment setup.\n\nChecking environment...\n";


my $implicit_dep = 'lib/common';

# work out platform variables
use lib 'infrastructure';
use BoxPlatform;

# don't allow old versions of openssl by default.
my $old_version_of_openssl_ok = 0;

# keep copy of command line args
my $makebuildenv_args = join(' ',@ARGV);

# do command line arguments
my $compile_line_extra = $platform_compile_line_extra;
my $link_line_extra = $platform_link_line_extra;
for(@ARGV)
{
	if($_ eq 'allow-old-openssl')
	{
		$old_version_of_openssl_ok = 1;
		next;
	}
	my ($k,$v) = split /:/,$_,2;
	if($k eq 'compile')
	{
		$compile_line_extra .= $v . ' ';
	}
	elsif($k eq 'link')
	{
		$link_line_extra .= $v . ' ';
	}
	elsif($k eq 'openssl')
	{
		# assume that the bin/lib/include dirs are under the specified path
		chop $v if ($v =~ /\/$/);
		$compile_line_extra = "-I$v/include $compile_line_extra"; 
		$link_line_extra    = "-L$v/lib $link_line_extra";
	}
	else
	{
		die "invalid option $_ specified on command line"
	}
}

# make sure local files directory exists
unless(-d 'local')
{
	mkdir 'local',0755;
}


# flags about the environment
my %env_flags;
# messages on test failure
my $test_failure_text;

# run all tests
{
	opendir DIR,'infrastructure/tests' or die "Can't read the tests directory";
	my @tests = grep {m/_tests\.pl\Z/} readdir DIR;
	closedir DIR;
	for(@tests)
	{
		require "infrastructure/tests/$_";
	}
}


# ---- LINUX start --------------------------------------------------------------------------
# do configuration on Linux... find out what's available, and configure appropraitely.
if($build_os eq 'Linux')
{
	# setup the basic library translation flags -- these will omit those libraries from the compile line
	$env_flags{'LIBTRANS_-lreadline'} = '';
	$env_flags{'LIBTRANS_-lLINUX_DB'} = '';

	my $curses_lib = '-lcurses';
	if((!exists($env_flags{'curses_PRESENT'})) && exists($env_flags{'ncurses_PRESENT'}))
	{
		# got to use ncurses instead...
		$env_flags{'LIBTRANS_-lcurses'} = '-lncurses';
		$curses_lib = '-lncurses';
	}

	my ($linux_readline_h, $linux_db_h);
	my $db_ver = -1;
	
	open H_FILES,"find /usr/include |" or die "Can't open find";
	while(<H_FILES>)
	{
		chomp;
		
		if(m~/readline.h\Z~)
		{
			my $rlh = strip_h_name($_);
			if(check_readline_h($rlh,$curses_lib))
			{
				$linux_readline_h = $rlh;
				# allow readline library to be used
				delete $env_flags{'LIBTRANS_-lreadline'};
			}
		}
		if(m~/db(|_1\d+).h\Z~)
		{
			# version?
			my $dv = 0;
			$dv = $1 if m~(\d+)/db~;
			my $fn = $_;

			# check this file mentions dbopen
			open DB_H,$_ or die "Can't open $_";
			my $found = 0;
			while(<DB_H>)
			{
				if(m/dbopen/)
				{
					$found = 1;
					last;
				}
			}
			close DB_H;
			next unless $found;

			# see if this version works
			print("Checking db version...\n");
			my $db_h = strip_h_name($fn);
			my $db_lib = 'db';
			$db_lib .= $db_ver if $db_ver > 0;
			if(!check_db_lib($db_h, $db_lib))
			{
				# try another
				$db_lib = 'db';
				next unless check_db_lib($db_h, $db_lib)
			}

	 		# good version?
			if($dv > $db_ver)
			{
				$linux_db_h = $db_h;
				$db_ver = $dv;
				$env_flags{'LIBTRANS_-lLINUX_DB'} = '-l'.$db_lib;
			}
		}
	}
	print "Finished checking headers\n";
	close H_FILES;

	# write the platform file accordingly and tell the user what's happened.
	open LIN,">local/_linux_platform.h" or die "Can't open file for writing";
	if($linux_readline_h eq '')
	{
		print("---------------------\nWARNING: readline isn't installed\n---------------------\n");
		print LIN "#define PLATFORM_READLINE_NOT_SUPPORTED\n";

	}
	else
	{
		open RL_H,">local/_linux_readline.h" or die "Can't open file for writing";
		print RL_H "#include <$linux_readline_h>\n";
		my $hist = $linux_readline_h;
		$hist =~ s/readline\.h/history.h/;
		print RL_H "#include <$hist>\n";
		close RL_H;
	}
	if($linux_db_h eq '')
	{
		print("---------------------\nWARNING: db is not installed -- will run in reduced efficiency mode without it.\n---------------------\n");
		print LIN "#define PLATFORM_BERKELEY_DB_NOT_SUPPORTED\n";
	}	
	else
	{
		open DB_H,">local/_linux_db.h" or die "Can't open file for writing";
		print DB_H "#include <$linux_db_h>\n";
		close DB_H;
	}
	close LIN;
}
sub strip_h_name
{
	my $i = $_[0];
	$i =~ s~\A/usr/include/~~;
	return $i;
}
sub check_readline_h
{
	my ($h,$curses_lib) = @_;
	print "Check readline from $h...\n";
	my $hist = $h;
	$hist =~ s/readline\.h/history.h/;
	open READLINEH,">readlineh.cpp" or die "Can't open readline test file for writing";
	print READLINEH <<__E;
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <$h>
#include <$hist>
int main(int argc, char *argv[])
{
	using_history();
	add_history(0);
	readline(0);
	return 0;
}
__E
	close READLINEH;
	# attempt to compile
	my $r = system("g++ readlineh.cpp -lreadline $curses_lib -o readlineh $compile_line_extra $link_line_extra 2>/dev/null >/dev/null");
	# delete test files
	unlink 'readlineh';
	unlink 'readlineh.cpp';
	return $r == 0; # compilation succeeded
}
sub check_db_lib
{
	my ($h,$db) = @_;
	open TESTDBLIB,">testdblib.cpp" or die "Can't open db test file for writing";
	print TESTDBLIB <<__E;
#include <$h>
int main(int argc, char *argv[])
{
	DB *dbp = 0;
	dbp = dbopen(0, 0, 0, DB_HASH, 0);
	dbp->close(dbp);
	DBT data;
	dbp->put(dbp, &data, &data, 0);
	dbp->get(dbp, &data, &data, 0);
	return 0;
}
__E
	close TESTDBLIB;
	# attempt to compile
	my $r = system("g++ testdblib.cpp -l$db -o testdblib $compile_line_extra $link_line_extra 2>/dev/null >/dev/null");
	# delete test files
	unlink 'testdblib';
	unlink 'testdblib.cpp';
	return $r == 0; # compilation succeeded
}
# ---- LINUX end --------------------------------------------------------------------------

# print "Flag: $_\n" for(keys %env_flags);

# check the version of openssl installed
if(!exists $env_flags{'OPENSSL_OK'})
{
	# failed somehow...

	# is an old version present?
	my $old_version = (exists $env_flags{'OLD_OPENSSL_OK'});

	if(!$old_version && !-e '/usr/include/openssl/evp.h')
	{
		# headers not installed?
		print "\n\nERROR: OpenSSL library and headers need to be installed.\n\nSee documentation on web site if you need to add extra search paths.\n\n";
		exit(1);
	}
	else
	{
		if($old_version)
		{
			if($old_version_of_openssl_ok)
			{
				print "\n\nWARNING: Configuring for old version of OpenSSL.\nPerformance will be lower than with version 0.9.7\n\n";
				$compile_line_extra .= '-DPLATFORM_OLD_OPENSSL ';
			}
			else
			{
				# tell user how they might try anyway with an old version of openssl
				print <<__E;

===============================================================================

You appear to have a version of OpenSSL installed which is less than 0.9.7.

It is possible to configure to use this old version, but performance will be
lower than if you have 0.9.7 -- the later version is recommended.

If you wish to use this old version, repeat the configuration with the extra
argument 'allow-old-openssl', like this:

   ./configure allow-old-openssl

* Please do not distribute binary packages with this option enabled.

* Please do not distribute ports which set this option by default.

IMPORTANT: Support for older versions of OpenSSL should be considered
experimental. It is not recommended for production use, but provided to allow
easier evaluation of this software before installing the latest OpenSSL.

===============================================================================

__E
				exit(1);
			}
		}
		else
		{
			print "\n\nERROR: You need to install OpenSSL, preferably at least version 0.9.7.\n\n";
			print "If you believe you have installed OpenSSL, check that the headers are installed\nas well ('dev' packages?)\n\nSee documentation on web site if you need to add extra search paths.\n\n";
			exit(1);
		}
	}
}


# finished checking the environment
print "done\n\n";


# seed autogen code
print "Seeding autogen code...\n";
open FINDAUTOGEN,"find . -follow -name Makefile.extra |" or die "Can't use find for locating files";
while(<FINDAUTOGEN>)
{
	chomp;
	my $file = $_;
	$file =~ m~\A(.+)/[^/]+\Z~;
	my $dir = $1;
	open FL,$file or die "Can't open $_ for reading";
	my %vars;
	my $do_cmds = 0;
	while(<FL>)
	{
		chomp;
		if(m/\A(.+)\s+=\s+(.+)\Z/)
		{
			# is a variable
			$vars{$1} = $2;
			next;
		}
		next unless m/\S/;
		if(m/AUTOGEN SEEDING/)
		{
			$do_cmds = 1;
		}
		elsif(m/\A\S/)
		{
			$do_cmds = 0 if $do_cmds == 2;
		}
		else
		{
			# command, run it?
			if($do_cmds)
			{
				$do_cmds = 2; # flag something has been done
				
				# subsitute variables, repeatedly
				my $c = $_;
				$c =~ s/\A\s+//;
				while(1)
				{
					my $did_subst = 0;
					
					for my $k (keys %vars)
					{
						$did_subst = 1 if $c =~ s/\$\($k\)/$vars{$k}/g;
					}
					
					last unless $did_subst;
				}
				
				# run command
				die "Couldn't run command $c" unless (0 == system("(cd $dir; $c)"))
			}
		}
	}
	close FL;
}
close FINDAUTOGEN;
print "done\n\n";


# open test mail program template file
my $test_template_file = 'infrastructure/buildenv-testmain-template.cpp';
open FL,$test_template_file or die "Can't open test template file\n";
my $test_template;
read FL,$test_template,-s $test_template_file;
close FL;


# extra platform defines
my $extra_platform_defines = '';
if($gcc_v3 && !$gcc_v4)
{
	$extra_platform_defines .= ' -DPLATFORM_GCC3'
}
if($gcc_v4)
{
	$extra_platform_defines .= ' -DPLATFORM_GCC4'
}

# read in module definitions file, and any files it includes
my @modules_files;
sub read_modules_file
{
	my ($mf) = @_;
	my $f = gensym;
	open $f,$mf or die "Can't open modules file '$mf'\n";
	while(<$f>)
	{
		if(m/\AINCLUDE\s+(\S+)\Z/)
		{
			# include another file
			read_modules_file($1)
		}
		else
		{
			push @modules_files,$_
		}
	}
	close $f;
}
read_modules_file('modules.txt');

# prepare directories...
mkdir "release",0755;
mkdir "debug",0755;

# is the library code in another directory?
my $external_lib = readlink('lib');
if($external_lib ne '')
{
	# adjust to root of the library distribution
	$external_lib =~ s!/lib\Z!!;
	$external_lib = '../'.$external_lib;
	# make symlinks
	make_obj_symlink('debug');
	make_obj_symlink('release');
}
sub make_obj_symlink
{
	my $m = $_[0];
	my $target = $external_lib."/$m/lib/";
	my $link = "$m/lib";
	# check link
	if(-e $link)
	{
		if(-l $link)
		{
			if(readlink($link) ne $target)
			{
				print "Warning: replacing $link with new link to $target\n";
				unlink $link;
			}
		}
		else
		{
			die "$link already exists, but it isn't a symbolic link"
		}
	}
	if(!-e $link)
	{
		symlink $target,$link or die "Can't make $m/lib symlink";
	}
}

print "Scanning code...\n";

my $modules_omitted = 0;

# process lines in flattened modules files
for(@modules_files)
{
	# clean up line
	chomp; s/\A\s+//; s/#.*\Z//; s/\s+\Z//; s/\s+/ /g;
	next unless m/\S/;
	
	# omit bits on some platforms?
	next if m/\AEND-OMIT/;
	if(m/\AOMIT:(.+)/)
	{
		if($1 eq $build_os)
		{
			$modules_omitted = 1;
			while(<MODULES>)
			{
				last if m/\AEND-OMIT/;	
			}
		}
		next;
	}
	
	# split up...
	my ($mod, @deps_i) = split / /;
	
	# ignore this module?
	next if ignore_module($mod);
	
	# deps for this platform
	my @deps;
	for(@deps_i)
	{
		my ($dep,$exclude_from) = split /!/;
		# generic library translation
		$dep = $env_flags{'LIBTRANS_'.$dep} if exists($env_flags{'LIBTRANS_'.$dep});
		next if $dep eq '';
		if($exclude_from =~ m/\A\+(.+)\Z/)
		{
			$exclude_from = $1;
			my $inc = 0;
			for(split /,/,$exclude_from)
			{
				$inc = 1 if $_ eq $build_os
			}
			push @deps,$dep if $inc
		}
		else
		{
			my $inc = 1;
			for(split /,/,$exclude_from)
			{
				$inc = 0 if $_ eq $build_os
			}
			push @deps,$dep if $inc
		}
	}
	
	# check directory exists
	die "Module $mod can't be found\n" unless -d $mod;
	
	# and put in lists
	push @modules,$mod;
	my @md;	# module dependencies
	my @lo; # link line options
	for(@deps)
	{
		if(/\A-l/)
		{
			push @lo,$_
		}
		else
		{
			push @md,$_ unless ignore_module($_)
		}
	}
	$module_dependency{$mod} = [$implicit_dep,@md];
	$module_library_link_opts{$mod} = [@lo];
	
	# make directories, but not if we're using an external library and this a library module
	my ($s,$d) = split /\//,$mod;
	if($s ne 'lib' || $external_lib eq '')
	{
		mkdir "release/$s",0755;
		mkdir "release/$s/$d",0755;
		mkdir "debug/$s",0755;
		mkdir "debug/$s/$d",0755;
	}
}

# make dirs for implicit dep
mkdir "release/$implicit_dep",0755;
mkdir "debug/$implicit_dep",0755;

# write a list of all the modules we've configured to use
open CONFIGURED_MODS,'>local/modules.h' or die "Can't write configured modules list";
print CONFIGURED_MODS <<__E;
// automatically generated file, do not edit
#ifndef _CONFIGURED_MODULES__H
#define _CONFIGURED_MODULES__H
__E
for($implicit_dep,@modules)
{
	my $m = $_;
	$m =~ s~/~_~;
	print CONFIGURED_MODS "#define MODULE_$m\n";
}
print CONFIGURED_MODS <<__E;
#endif // _CONFIGURED_MODULES__H
__E
close CONFIGURED_MODS;


# now make a list of all the .h files we can find, recording which module they're in
my %hfiles;
for my $mod (@modules, $implicit_dep)
{
	opendir DIR,$mod;
	my @items = readdir DIR;
	closedir DIR;

	# add in items from autogen directories, and create output directories
	{
		my @autogen_items;
		
		for my $di (@items)
		{
			if($di =~ m/\Aautogen/ && -d "$mod/$di")
			{
				# Read items
				my $d = "$mod/$di";
				opendir DIR,$d;
				my @i = readdir DIR;
				closedir DIR;
				for(@i)
				{
					next if m/\A\./;
					push @autogen_items,"$di/$_"
				}
			}
		}
		@items = (@items, @autogen_items);
	}

	for(grep /\.h\Z/i, @items)
	{
		next if /\A\._/;	# Temp Mac OS Resource hack
		die "Header file $_ already used in module ".$hfiles{$_}."\n" if exists $hfiles{$_};
		$hfiles{$_} = $mod
	}
}

for my $mod (@modules, $implicit_dep)
{
	opendir DIR,$mod;
	for my $h (grep /\.h\Z/i, readdir DIR)
	{	
		next if /\A\._/;	# Temp Mac OS Resource hack

		open FL,"$mod/$h" or die "can't open $mod/$h";
		my $f;
		read FL,$f,-s "$mod/$h";
		close FL;

		while($f =~ m/\#include\s+"([^"]+?)"/g)
		{
			my $i = $1;
			# ignore autogen exceptions
			next if $i =~ m/\Aautogen_.+?Exception.h\Z/;
			# record dependency
			${$header_dependency{$h}}{$i} = 1 if exists $hfiles{$i};
		}
	}
	closedir DIR;
}

print "done\n\nGenerating Makefiles...\n";


# Then write a makefile for each module
for my $mod (@modules, $implicit_dep)
{
	print $mod,"\n";
	
	my ($type,$name) = split /\//,$mod;
	
	# add additional files for tests
	if($type eq 'test')
	{
		my $testmain = $test_template;
		$testmain =~ s/TEST_NAME/$name/g;
		open TESTMAIN,">$mod/_main.cpp" or die "Can't open test main file for $mod for writing\n";
		print TESTMAIN $testmain;
		close TESTMAIN;
		
		# test file...
		sub writetestfile
		{
			my ($filename,$runcmd,$module) = @_;		
			open TESTFILE,">$filename" or die "Can't open test script file for $module for writing\n";
			print TESTFILE "#!/bin/sh\necho TEST: $module\n";
			if(-d "$module/testfiles")
			{
				print TESTFILE <<__E;
echo Removing old test files...
rm -rf testfiles
echo Copying new test files...
cp -p -R ../../../$module/testfiles .
__E
			}
			if(-e "$module/testextra")
			{
				open FL,"$module/testextra" or die "Can't open $module/testextra";
				while(<FL>) {print TESTFILE}
				close FL;
			}
			print TESTFILE "$runcmd\n";
			close TESTFILE;
		}
		
		writetestfile("$mod/_t", './test $1 $2 $3 $4 $5', $mod);
		writetestfile("$mod/_t-gdb", 'gdb ./test', $mod);
		
	}
	
	my @all_deps_for_module;
	{
		# work out what dependencies need to be run
		my @deps_raw;
		sub add_mod_deps
		{
			my ($arr_r,$nm) = @_;
			if($#{$module_dependency{$nm}} >= 0)
			{
				push @$arr_r,@{$module_dependency{$nm}};
				for(@{$module_dependency{$nm}})
				{
					add_mod_deps($arr_r,$_)
				}
			}
		}
		add_mod_deps(\@deps_raw, $mod);
		# and then dedup and reorder them
		my %d_done;
		for(my $a = $#deps_raw; $a >= 0; $a--)
		{
			if(!exists $d_done{$deps_raw[$a]})
			{
				# insert
				push @all_deps_for_module, $deps_raw[$a];
				# mark as done
				$d_done{$deps_raw[$a]} = 1;
			}
		}
	}	
	

	# make include path
	my $include_paths = join(' ',map {'-I../../'.$_} @all_deps_for_module);

	# is target a library?
	my $target_is_library = ($type ne 'bin' && $type ne 'test');

	# make target name
	my $end_target = $name;
	$end_target .= '.a' if $target_is_library;
	$end_target = 'test' if $type eq 'test';
	# adjust for outdir
	$end_target = '$(OUTDIR)/' . $end_target;

	# start the makefile
	my $mk_name_extra = ($bsd_make)?'':'X';
	open MAKE,">$mod/Makefile".$mk_name_extra or die "Can't open Makefile for $mod\n";
	my $debug_link_extra = ($target_is_library)?'':'../../debug/lib/debug/debug.a';
	print MAKE <<__E;
#
# AUTOMATICALLY GENERATED FILE
#    do not edit!
#
#
CXX = g++
AR = ar
RANLIB = ranlib
.ifdef RELEASE
CXXFLAGS = -DNDEBUG -O2 -Wall $include_paths -D$platform_define$extra_platform_defines -DBOX_VERSION="\\"$product_version\\""
OUTBASE = ../../release
OUTDIR = ../../release/$mod
DEPENDMAKEFLAGS = -D RELEASE
VARIENT = RELEASE
.else
CXXFLAGS = -g -Wall $include_paths -D$platform_define$extra_platform_defines -DBOX_VERSION="\\"$product_version\\""
OUTBASE = ../../debug
OUTDIR = ../../debug/$mod
DEPENDMAKEFLAGS =
VARIENT = DEBUG
.endif

__E
	
	# read directory
	opendir DIR,$mod;
	my @items = readdir DIR;
	closedir DIR;
	
	# add in items from autogen directories, and create output directories
	{
		my @autogen_items;
		for my $di (@items)
		{
			if($di =~ m/\Aautogen/ && -d "$mod/$di")
			{
				# Read items
				my $d = "$mod/$di";
				opendir DIR,$d;
				my @i = readdir DIR;
				closedir DIR;
				for(@i)
				{
					next if m/\A\./;
					push @autogen_items,"$di/$_"
				}
				
				# output directories
				mkdir "release/$mod/$di",0755;
				mkdir "debug/$mod/$di",0755;
			}
		}
		@items = (@items, @autogen_items);
	}
	
	# first, obtain a list of depenencies within the .h files
	my %headers;
	for my $h (grep /\.h\Z/i, @items)
	{
		open FL,"$mod/$h";
		my $f;
		read FL,$f,-s "$mod/$h";
		close FL;

		while($f =~ m/\#include\s+"([^"]+?)"/g)
		{
			${$headers{$h}}{$1} = 1 if exists $hfiles{$1};
		}
	}
	
	# ready for the rest of the details...
	my $make;
	
	# then... do the cpp files...
	my @obj_base;
	for my $cpp (@items)
	{
		next unless $cpp =~ m/\A(.+)\.cpp\Z/i;
		next if $cpp =~ /\A\._/;	# Temp Mac OS Resource hack

		# store for later
		my $base = $1;
		push @obj_base,$base;
	
		# get the file...
		open FL,"$mod/$cpp";
		my $f;
		read FL,$f,-s "$mod/$cpp";
		close FL;
		
		my %dep;

		while($f =~ m/\#include\s+"([^"]+?)"/g)
		{
			insert_dep($1, \%dep) if exists $hfiles{$1};
		}
		
		# output filename
		my $out_name = '$(OUTDIR)/'.$base.'.o';
		
		# write the line for this cpp file
		$make .= $out_name.': '.join(' ',$cpp,map
			{ ($hfiles{$_} eq $mod)?$_:'../../'.$hfiles{$_}."/$_" } keys %dep)."\n";
		$make .= "\t\$(CXX) \$(CXXFLAGS) $compile_line_extra -c $cpp -o $out_name\n\n";

	}

	my $has_deps = ($#{$module_dependency{$mod}} >= 0);
# ----- # always has dependencies with debug library
	$has_deps = 1;

	# Depenency stuff
	my $deps_makeinfo;
	if($has_deps)
	{
		if($bsd_make)
		{
			$deps_makeinfo = <<'__E';
.BEGIN::
.ifndef NODEPS
.	if $(.TARGETS) == ""
__E
		}
		else
		{
			# gnu make
			$deps_makeinfo = <<'__E';
.PHONY:	dep_modules
dep_modules:
ifndef NODEPS
ifeq ($(strip $(.TARGETS)),)
__E
		}
		
		# run make for things we require
		for my $dep (@all_deps_for_module)
		{
			$deps_makeinfo .= "\t\t(cd ../../$dep; $make_command$sub_make_options \$(DEPENDMAKEFLAGS) -D NODEPS)\n";
		}
		$deps_makeinfo .= ".\tendif\n.endif\n\n";
	}
	print MAKE $deps_makeinfo if $bsd_make;
	
	# get the list of library things to add -- in order of dependency so things link properly
	my $lib_files = join(' ',map {($_ =~ m/lib\/(.+)\Z/)?('$(OUTBASE)/'.$_.'/'.$1.'.a'):undef} (reverse(@all_deps_for_module)));

	# need to see if the extra makefile fragments require extra object files
	# or include any more makefiles
	my @objs = @obj_base;
	my @makefile_includes;
	
	additional_objects_from_make_fragment("$mod/Makefile.extra", \@objs, \@makefile_includes);
	additional_objects_from_make_fragment("$mod/Makefile.extra.$build_os", \@objs, \@makefile_includes);

	my $o_file_list = join(' ',map {'$(OUTDIR)/'.$_.'.o'} @objs);
	print MAKE $end_target,': ',$o_file_list;
	print MAKE ' dep_modules' if !$bsd_make;
	print MAKE " ",$lib_files unless $target_is_library;
	print MAKE "\n";
	
	# stuff to make the final target...
	if($target_is_library)
	{
		# make a library archive...
		print MAKE "\t(echo -n > $end_target; rm $end_target)\n";
		print MAKE "\t\$(AR) -q $end_target $o_file_list\n";
		print MAKE "\t\$(RANLIB) $end_target\n";
	}
	else
	{
		# work out library options
		# need to be... least used first, in absolute order they appear in the modules.txt file
		my @libops;
		sub libops_fill
		{
			my ($m,$r) = @_;
			push @$r,$_ for(@{$module_library_link_opts{$m}});
			libops_fill($_,$r) for(@{$module_dependency{$m}});
		}
		libops_fill($mod,\@libops);
		my $lo = '';
		my %ldone;
		for(@libops)
		{
			next if exists $ldone{$_};
			$lo .= ' '.$_;
			$ldone{$_} = 1;
		}
	
		# link line...
		print MAKE "\t\$(CXX) $link_line_extra -o $end_target $o_file_list $lib_files$lo\n";
	}
	# tests need to copy the test file over
	if($type eq 'test')
	{
		print MAKE "\tcp _t \$(OUTDIR)/t\n\tchmod u+x \$(OUTDIR)/t\n";
		print MAKE "\tcp _t-gdb \$(OUTDIR)/t-gdb\n\tchmod u+x \$(OUTDIR)/t-gdb\n";
	}
	# dependency line?
	print MAKE "\n";

	# module dependcies for GNU make?
	print MAKE $deps_makeinfo if !$bsd_make;
	
	# print the rest of the file
	print MAKE $make,"\n";
	
	# and a clean target
	print MAKE "clean:\n\t-rm -rf \$(OUTDIR)/*\n.\tifndef SUBCLEAN\n";
	for my $dep (@all_deps_for_module)
	{
		print MAKE "\t(cd ../../$dep; $make_command \$(DEPENDMAKEFLAGS) -D SUBCLEAN clean)\n";
	}
	print MAKE ".\tendif\n";
	
	# include any extra stuff
	print MAKE "\n\n";
	if(-e "$mod/Makefile.extra")
	{
		print MAKE ".include <Makefile.extra>\n\n";
	}
	if(-e "$mod/Makefile.extra.$build_os")
	{
		print MAKE ".include <Makefile.extra.$build_os>\n\n";
	}
	for(@makefile_includes)
	{
		print MAKE ".include <$_>\n\n";
	}
	
	# and finally a target for rebuilding the build system
	print MAKE "\nbuildsystem:\n\t(cd ../..; perl ./infrastructure/makebuildenv.pl $makebuildenv_args)\n\n";
	
	close MAKE;

	if(!$bsd_make)
	{
		# need to post process this into a GNU makefile
		open MAKE,">$mod/Makefile";
		open MAKEB,"$mod/MakefileX";

		while(<MAKEB>)
		{
			s/\A\.\s*(ifdef|else|endif|ifndef)/$1/;
			s/\A\.\s*include\s+<(.+?)>/include $1/;
			s/-D\s+(\w+)/$1=1/;
			print MAKE;
		}

		close MAKEB;
		close MAKE;
		unlink "$mod/MakefileX";
	}
}

print "\nType 'cd <module_dir>; $make_command' to build a module\n\n";

print $test_failure_text;

if($modules_omitted)
{
	print "\nNOTE: Some modules have been omitted on this platform\n\n"
}

sub insert_dep
{
	my ($h,$dep_r) = @_;
	
	# stop random recusion
	return if exists $$dep_r{$h};
	
	# insert more depencies
	insert_dep($_,$dep_r) for keys %{$header_dependency{$h}};

	# mark this one as a dependency
	$$dep_r{$h} = 1;
}


sub additional_objects_from_make_fragment
{
	my ($fn,$objs_r,$include_r) = @_;
	
	if(-e $fn)
	{
		open FL,$fn or die "Can't open $fn";
		
		while(<FL>)
		{
			chomp;
			if(m/link-extra:\s*(.+)\Z/)
			{
				my @o = split /\s+/,$1;
				for(@o)
				{
					push @$objs_r,$1 if m/\A(.+)\.o\Z/;
				}
			}
			elsif(m/include-makefile:\s*(\S+)/)
			{
				push @$include_r,$1
			}
		}
		
		close FL;
	}
}


sub ignore_module
{
	exists $env_flags{'IGNORE_'.$_[0]}
}

# how to run a test
sub do_test
{
	my %t = @_;
	
	print $t{'Name'},':';
	open TEST_CODE,">envtest.cpp" or die "Can't open envtest.cpp for writing";
	print TEST_CODE $t{'Code'};
	close TEST_CODE;
	my $result = (system("g++ envtest.cpp ".$t{'TestCompileFlags'}." -o envtest $compile_line_extra $link_line_extra 2>/dev/null >/dev/null") == 0);
	if($result && exists $t{'RunCode'})
	{
		$result = 0 unless (system('./envtest') == 0);
	}
	unlink 'envtest.cpp';
	unlink 'envtest';
	print $result?" yes\n":" no\n";
	if($result)
	{
		# success
		if(exists $t{'SuccessFlags'})
		{
			for(@{$t{'SuccessFlags'}})
			{
				my ($k,$v) = split /=>/,$_;
				$v = 1 if $v eq '';
				$env_flags{$k} = $v
			}
		}
		$compile_line_extra .= $t{'SuccessCompileFlags'}.' ' if exists $t{'SuccessCompileFlags'};
		$link_line_extra .= $t{'SuccessLinkFlags'}.' ' if exists $t{'SuccessLinkFlags'};
	}
	else
	{
		# aborting failure?
		if(exists $t{'AbortOnFailure'})
		{
			print $t{'FailureText'};
			print "\nAborting configuration, cannot build in this environment.\n";
			exit(1);
		}
	
		# failure
		if(exists $t{'FailureFlags'})
		{
			for(@{$t{'FailureFlags'}})
			{
				my ($k,$v) = split /=>/,$_;
				$v = 1 if $v eq '';
				$env_flags{$k} = $v
			}
		}
		$compile_line_extra .= $t{'FailureCompileFlags'}.' ' if exists $t{'FailureCompileFlags'};
		$link_line_extra .= $t{'FailureLinkFlags'}.' ' if exists $t{'FailureLinkFlags'};
		$test_failure_text .= $t{'FailureText'} if exists $t{'FailureText'};
	}
	$result
}



