#!/usr/bin/perl
use strict;

my ($in,$out_lang,$out_choices) = @ARGV;
if($in eq '' || $out_lang eq '' || $out_choices eq '')
{
	die "usage : preprocesslangfortrans.pl <lang file> <output lang file> <output choices file>\n";
}

open IN,$in or die "Can't open $in";
open OUT,'>'.$out_lang or die "Can't open $out_lang for writing";
open CHOICES,'>'.$out_choices or die "Can't open $out_choices for writing";

print CHOICES <<__E;

This file contains the lists of choices from the language file, $out_lang.

For each line, translate the SECOND word, leaving the original as it stands.

For example, change

   Monday : Monday

to

   Monday : Lundi

when you are translating that line.

==========================================================
__E

while(<IN>)
{
	if(m/\A[^#>@=].*\|/)
	{
		# it's a choices line!
		print OUT "---- DO NOT TRANSLATE THIS ENTRY, translate $out_choices instead ----\n";
		
		chomp;
		my @choices = split /\|/,$_;
		for(@choices)
		{
			print CHOICES "$_ : $_\n";
		}
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
