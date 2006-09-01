#!/usr/bin/perl
use strict;

my ($in,$in_choices,$out_lang) = @ARGV;
if($in eq '' || $in_choices eq '' || $out_lang eq '')
{
	die "usage : processlangfromtrans.pl <input lang file> <input choices file> <output lang file>\n";
}

open IN,$in or die "Can't open $in";
open CHOICES,$in_choices or die "Can't open $in_choices for reading";
open OUT,'>'.$out_lang or die "Can't open $out_lang for writing";

# Read choices...
# skip preamble
while(<CHOICES>)
{
	last if m/\A==================/;
}
# read in entries
my %choices;
while(<CHOICES>)
{
	chomp;
	next unless m/\A(.+?)\s+:\s+(.+?)\Z/;
	$choices{$1} = $2;
}

# process the file
my $last_choice_trans = '';
while(<IN>)
{
	if(m/\A> .*\|/)
	{
		print OUT;
		
		# it's a choices line
		my $l = $_;
		chomp $l;
		$l =~ s/\A> //;
		my @choices = split /\|/,$l;
		$last_choice_trans = '';
		for my $c (@choices)
		{
			print "choice $c not translated in $l\n" unless exists $choices{$c};
			$last_choice_trans .= '|' if $last_choice_trans ne '';
			$last_choice_trans .= $choices{$c};
		}
	}
	elsif(m/\A---- DO NOT TRANSLATE THIS ENTRY/)
	{
		# put the translated choices back
		print OUT $last_choice_trans,"\n";
	}
	else
	{
		# copy line to output
		print OUT
	}
}

close CHOICES;
close OUT;
close IN;
