#!/usr/bin/perl
use strict;
use Symbol;

my $template_file = 'lib/webappframework/StarterTemplate.txt';
my $template = gensym;
unless(open($template,$template_file))
{
	print "Cannot open template $template_file\nYou must run this script from the root of the distribution.\n";
	exit(1);
}

my $dir = ask_qn('Directory to create application in? (eg bin/newwebapp)',
	"Directory given must start with 'bin/' and not already exist", sub
		{
			my $d = $_[0];
			return 0 unless $d =~ m`\Abin/\w+\Z`;
			return 0 if -e $d;
			1;
		}
	);
my $appname = ask_qn('Long name of application? (eg NewWebApp)');
my $shortappname = ask_qn('Daemon name of application? (eg newwebapp)');
my $url_base = ask_qn('URL base? (eg app for /app/... URLs)');
my $use_database = ask_qn('Include database support? (answer yes or no)', 'Enter yes or no', \&check_yesno) eq 'yes';

# create the directory
mkdir $dir,0755 or die "Can't create directory $dir\n";

# setup flags
my %if_flags;
$if_flags{'database'} = 1 if $use_database;
$if_flags{'no_database'} = 1 unless $use_database;

my $output = gensym;
my $output_enabled = 1;

while(<$template>)
{
	s/APP_LONG_NAME/$appname/g;
	s/APP_SHORT_NAME/$shortappname/g;
	s/APP_URL_BASE/$url_base/g;

	if(m/\A\*\*\*\s+(.+?)\n/)
	{
		# command
		my @cmd = split /\s+/,$1;
		if(!$output_enabled && $cmd[0] eq 'endif')
		{
			# enable output again
			$output_enabled = 1
		}
		elsif($output_enabled)
		{
			# process other commands
			if($cmd[0] eq 'file')
			{
				# open a file
				close $output;
				open $output,">$dir/".$cmd[1] or die "Can't open $cmd[1] for writing";
			}
			elsif($cmd[0] eq 'mkdir')
			{
				# make directory
				mkdir "$dir/".$cmd[1] or die "Can't create directory $cmd[1]";
			}
			elsif($cmd[0] eq 'if')
			{
				# need this section?
				$output_enabled = exists $if_flags{$cmd[1]}
			}
		}
	}
	else
	{
		print $output $_ if $output_enabled
	}
}

close $output;
close $template;

my $db_inc = ($use_database)?' lib/database':'';
my $db_setup;
if($use_database)
{
	$db_setup = <<__E;

    ./$shortappname create_db sqlite $shortappname.sqlite
__E
}

print <<__E;

Template created. Additional steps:

1) Add this line to modules.txt
$dir lib/webappframework$db_inc

2) Configure the build system
     ./configure

3) Build the module
     (cd $dir; make)
(add RELEASE=1 to build the release version)

4) Try it out
     cd debug/$dir$db_setup
     ./$shortappname ../../../$dir/$shortappname.conf
and view
     http://localhost:1080/$url_base/en/main
in your web browser

5) Kill with 
     xargs kill < $shortappname.pid

__E

sub ask_qn
{
	my ($qn,$errmsg,$check) = @_;
	my $answer;
	while($answer eq '')
	{
		print $qn,"\n";
		$answer = <STDIN>;
		$answer =~ s/\A\s+//;
		$answer =~ s/\s+\Z//;
		if($answer eq '' || (ref($check) && !&$check($answer)))
		{
			$answer = '';
			print "*** ERROR: ",(($errmsg ne '')?$errmsg:'Please enter a value'),"\n";
		}
	}
	$answer;
}

sub check_yesno
{
	my $answer = lc($_[0]);
	return $answer eq 'yes' || $answer eq 'no'
}

