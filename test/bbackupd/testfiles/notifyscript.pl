#!/usr/bin/perl


my $f = 'testfiles/notifyran.'.$ARGV[0].'.';
my $n = 1;

while(-e $f.$n)
{
	$n ++;
}

open FL,'>'.$f.$n;
print FL localtime();
close FL;

