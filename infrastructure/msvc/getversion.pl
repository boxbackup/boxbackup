#!perl

$basedir = $0;
$basedir =~ s|/|\\|g;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
$basedir =~ s/\\[^\\]*$//;
-d $basedir or die "$basedir: $!";
chdir $basedir or die "$basedir: $!";

require "$basedir\\infrastructure\\BoxPlatform.pm.in";

my $verfile = "$basedir/lib/common/BoxVersion.h";
my $newver = "#define BOX_VERSION \"$BoxPlatform::product_version\"\n";
my $oldver = "";

if (-r $verfile)
{
	open VERSIONFILE, "< $verfile" or die "$verfile: $!";
	$oldver = <VERSIONFILE>;
	close VERSIONFILE;

}

if ($newver ne $oldver)
{
	open VERSIONFILE, "> $verfile" or die "BoxVersion.h: $!";
	print VERSIONFILE $newver;
	close VERSIONFILE;
}

print $BoxPlatform::product_version;
exit 0;
