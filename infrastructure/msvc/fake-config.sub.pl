#!perl

use strict;
use warnings;
use Cwd;

require "infrastructure\\BoxPlatform.pm.in";
my $wd = getcwd();
my $dummy = $BoxPlatform::product_version;

while(<>)
{
	s|\@build_dir@|$wd|;
	s|\@client_parcel_dir@|$wd/Debug|;
	s|\@box_version@|$BoxPlatform::product_version|;
	m|[^@](@[^@]+@)| and die "Unknown variable: $1";
	print;
}