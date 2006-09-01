#!/usr/bin/perl
use strict;
use Symbol;
use lib '../../lib/perl';
use lib '../../lib/webappframework';
use lib '../../lib/database';
use WebAppFramework;
use CppVariable;
use vars qw/$webapp $page $language/;

my ($app_description,$action,$page_name,$language_a) = @ARGV;
die "Application description not specified, or not found\n" unless -e $app_description;
die "Action not specified\n" unless $action ne '';
$language = $language_a;
$language = 'COMMON' if $language eq '';

# Create the application object
$webapp = WebAppFramework->new();

# Pull the descripition of the application
require $app_description;

# create the autogen directory, if it doesn't exist
if(!-d 'autogen_webapp')
{
	mkdir 'autogen_webapp',0744 or die "Can't create autogen directory\n";
}

# Perform the required action
if($action eq 'make')
{
	# Write the makefile and generate the code for all the pages
	print "Generating web application makefile and main server class...\n";
	my $makefile = gensym;
	open $makefile,">Makefile.webapp" or die "Can't open Makefile.webapp for writing\n";
	
	$webapp->write_makefile($makefile, $app_description);
	
	close $makefile;

	$webapp->write_server_class();

	# generate the global functions and definitions file
	$page = setup_page();
	$page->call_in_default_page();
	$webapp->write_global_code($page);
}
elsif($action eq 'page')
{
	# Write the source files for the page
	my $action = ($language eq 'COMMON')?'common code':"language $language";
	print "Generating web application page $page_name ($action)...\n";
	
	# tell the web application about the language we're doing
	$webapp->set_current_langage($language);
	
	# Check the page exists.
	die "Page description script for $page_name not found.\n" unless -f "Pages/$page_name.pl";
	
	# Create the basic page
	$page = setup_page();
	$page->call_in_default_page();
	
	# Tell it about the web application
	$webapp->initialise_page($page, $page_name);
		
	# Read in the page script
	require "Pages/$page_name.pl";
	
	# Write it
	$webapp->write_page($page, $page_name, $language);
}
else
{
	die "Unknown action '$action'\n";
}


