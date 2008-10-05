#!perl

unless (@ARGV == 2)
{
	die "Usage: $0 <parcel-name> <target-os>\n";
}

$basedir = $0;
$basedir =~ s|/.*||;
$basedir .= "/..";
-d $basedir or die "$basedir: $!";
chdir $basedir or die "$basedir: $!";
require "infrastructure/BoxPlatform.pm.in";

print BoxPlatform::parcel_dir(@ARGV) . "\n";

exit 0;
