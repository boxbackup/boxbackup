#!perl

$basedir = $0;
$basedir =~ s|/.*||;
$basedir .= "/..";
-d $basedir or die "$basedir: $!";
chdir $basedir or die "$basedir: $!";
require "infrastructure/BoxPlatform.pm.in";

print "$BoxPlatform::product_version\n";

exit 0;
