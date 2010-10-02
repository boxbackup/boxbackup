#!perl

$basedir = $0;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
-d $basedir or die "$basedir: $!";
chdir $basedir or die "$basedir: $!";

require "$basedir\\infrastructure\\BoxPlatform.pm.in";

open VERSIONFILE, "> $basedir/lib/common/BoxVersion.h" 
	or die "BoxVersion.h: $!";
print VERSIONFILE "#define BOX_VERSION \"$BoxPlatform::product_version\"\n";
close VERSIONFILE;

exit 0;
