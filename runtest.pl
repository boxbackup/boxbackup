#!/usr/bin/perl

use lib 'infrastructure';
use BoxPlatform;

my ($test_name,$test_mode) = @ARGV;

$test_mode = 'debug' if $test_mode eq '';

if($test_name eq '' || ($test_mode ne 'debug' && $test_mode ne 'release'))
{
	print <<__E;
Run Test utility -- bad usage.

runtest.pl (test|ALL) [release|debug]

Mode defaults to debug.

__E
	exit(0);
}

my @results;

if($test_name ne 'ALL')
{
	# run one test
	runtest($test_name);
}
else
{
	# run all tests
	my @tests;
	open MODULES,'modules.txt' or die "Can't open modules file";
	while(<MODULES>)
	{
		# omit bits on some platforms?
		next if m/\AEND-OMIT/;
		if(m/\AOMIT:(.+)/)
		{
			if($1 eq $build_os)
			{
				while(<MODULES>)
				{
					last if m/\AEND-OMIT/;	
				}
			}
			next;
		}
		push @tests,$1 if m~\Atest/(\w+)\s~;
	}
	close MODULES;
	
	runtest($_) for(@tests)
}

# report results
print "--------\n",join("\n",@results),"\n";

sub runtest
{
	my ($t) = @_;

	# attempt to make this test
	my $flag = ($test_mode eq 'release')?(BoxPlatform::make_flag('RELEASE')):'';
	my $make_res = system("cd test/$t ; $make_command $flag");
	if($make_res != 0)
	{
		push @results,"$t: make failed";
		return;
	}
	
	# run it
	my $test_res = system("cd $test_mode/test/$t ; ./t | tee ../../../temp.runtest");

	# open test results
	if(open RESULTS,'temp.runtest')
	{
		my $last;
		while(<RESULTS>)
		{
			$last = $_ if m/\w/;
		}
		close RESULTS;
		chomp $last;
		push @results,"$t: $last";
	}
	else
	{
		push @results,"$t: output not found";
	}
	
	# delete test results
	unlink 'temp.runtest';
}

