#!/usr/bin/perl
# distribution boxbackup-0.09
# 
#  
# Copyright (c) 2003, 2004
#      Ben Summers.  All rights reserved.
#  
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All use of this software and associated advertising materials must 
#    display the following acknowledgement:
#        This product includes software developed by Ben Summers.
# 4. The names of the Authors may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
# 
# [Where legally impermissible the Authors do not disclaim liability for 
# direct physical injury or death caused solely by defects in the software 
# unless it is modified by a third party.]
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#  
#  
#  

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

