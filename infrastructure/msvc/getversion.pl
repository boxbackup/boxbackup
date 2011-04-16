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

my $newver = "#define BOX_VERSION \"$BoxPlatform::product_version\"\n";

open VERSIONFILE, "< $basedir/lib/common/BoxVersion.h" 
	or die "BoxVersion.h: $!";
my $oldver = <VERSIONFILE>;
close VERSIONFILE;

if ($newver ne $oldver)
{
	open VERSIONFILE, "> $basedir/lib/common/BoxVersion.h" 
		or die "BoxVersion.h: $!";
	print VERSIONFILE $newver;
	close VERSIONFILE;
}

print $BoxPlatform::product_version;
exit 0;
