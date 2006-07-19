#!/usr/bin/perl
use strict;

my $flags = $ARGV[0] or "";

unless(open IN,"../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query4.log \"compare -ac$flags\" quit|")
{
	print "Couldn't open compare utility\n";
	exit 2;
}

my $ret = 1;
my $seen = 0;

while(<IN>)
{
	next unless m/\S/;
	if(m/continousupdate/)
	{
		unless (/exists/)
		{
			print "FAIL: continousupdate line does not match\n";
			$ret = 2;
		}
		$seen = 1;
	}
	else
	{
		unless (/\AWARNING/ or /\ADifferences/ or /might be reason/
			or /probably due to file mod/)
		{
			print "FAIL: Summary line does not match\n";
			$ret = 2;
		}
	}
	print "READ: $_";
}

close IN;

$ret = 2 unless $seen;

exit $ret;

