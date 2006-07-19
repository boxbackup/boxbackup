#!/usr/bin/perl
use strict;

unless(open IN,"../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query4.log \"compare -ac\" quit|")
{
	print "Couldn't open compare utility\n";
	exit 2;
}

my $ret = 1;

while(<IN>)
{
	next unless m/\S/;
	if(m/continousupdate/)
	{
		unless (m/contents/ or m/attributes/)
		{
			print "FAIL: continuousupdate line does not match\n";
			$ret = 2;
		}
	}
	else
	{
		unless (/\AWARNING/ or /\ADifferences/ or /might be reason/ 
			or /probably due to file mod/)
		{
			print "FAIL: summary line does not match\n";
			$ret = 2;
		}
	}

	print "READ: $_";
}

close IN;

exit $ret;

