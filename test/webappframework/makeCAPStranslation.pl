#!/usr/bin/perl
use strict;

my $template_in = 'Templates/TestWebAppMain.en.html';
my $template_out = 'Templates/TestWebAppMain.CAPS.html';
my $language_file = 'Languages/CAPS.txt';

if(0)
{ ### no longer do the templates in a translation -- uses marker system instead
# 'translate' the main HTML template
open FL,$template_in or die "Can't open HTML template file";
my $t;
read FL,$t,-s $template_in;
close FL;

# turn into upper case
$t =~ s/>(.*?)</'>'.uc($1).'<'/gse;
# put back HTML entities to lowercase
$t =~ s/&(\w+?);/'&'.lc($1).';'/ge;

open OUT,'>'.$template_out or die "Can't open output HTML template file";
print OUT $t;
close OUT;
}  ### no longer do the templates in a translation -- uses marker system instead

# 'translate' the language file
open FL,$language_file or die "Can't open language file";
open OUT,'>'.$language_file.'new' or die "Can't open temporary output language file";
while(<FL>)
{
	if(m/\A(#|@|>|========)/)
	{
		print OUT
	}
	else
	{
		my $x = $_;
		my %protect;
		my $pn = 0;
		$x =~ s/\{(.+?)\}/$protect{$pn}=$1;'{'.$pn++.'}'/ge;
		$x = uc($x);
		$x =~ s/\{(.+?)\}/'{'.$protect{$1}.'}'/ge;
		print OUT $x
	}
}
close OUT;
close FL;
rename $language_file.'new',$language_file;
