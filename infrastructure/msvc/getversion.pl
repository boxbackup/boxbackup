#!perl

$basedir = $0;
$basedir =~ s/(\\[^\\]*){5}$// or die "failed to remove last five ".
	"directories from $basedir";
-d $basedir or die "$basedir: not a directory";
chdir $basedir or die "cannot change to directory $basedir: $!";

require "$basedir\\infrastructure\\BoxPlatform.pm.in";

my $verfile = "$basedir\\lib\\common\\BoxVersion.h";
open VERSIONFILE, "> $verfile" or die "opening $verfile: $!";
print VERSIONFILE "#define BOX_VERSION \"$BoxPlatform::product_version\"\n";
close VERSIONFILE;

exit 0;
