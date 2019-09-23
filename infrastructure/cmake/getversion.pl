#!perl

use File::Basename;
$basedir = dirname($0)."/../..";

-d $basedir or die "Base directory $basedir does not exist!";
chdir $basedir or die "Failed to change to base directory: $basedir: $!";

require "$basedir/infrastructure/BoxPlatform.pm.in";

print "$BoxPlatform::product_version\n";

exit 0;
