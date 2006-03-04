#!@PERL@
use strict;

unless(open IN,"../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query4.log \"compare -ac\" quit|")
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
		$ret = 2 unless m/exists/;
		$seen = 1;
	}
	else
	{
		$ret = 2 unless m/\AWARNING/ || m/\ADifferences/ || /might be reason/ || /probably due to file mod/;
	}
	print;
}

close IN;

$ret = 2 unless $seen;

exit $ret;

