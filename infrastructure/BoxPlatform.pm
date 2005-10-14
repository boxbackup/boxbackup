package BoxPlatform;
use Exporter;
@ISA = qw/Exporter/;
@EXPORT = qw/$build_os $make_command $bsd_make $platform_define $gcc_v3 $gcc_v4 $product_version $product_name $install_into_dir $sub_make_options $platform_compile_line_extra $platform_link_line_extra/;

BEGIN
{

	# which OS are we building under?
	$build_os = `uname`;
	chomp $build_os;
	# Cygwin Builds usually something like CYGWIN_NT-5.0, CYGWIN_NT-5.1
	# Box Backup tried on Win2000,XP only :)
    $build_os = 'CYGWIN' if $build_os =~ m/CYGWIN/;

	$make_command = ($build_os ne 'Darwin')?'make':'bsdmake';
	$bsd_make = ($build_os ne 'Linux' && $build_os ne 'CYGWIN');
    $platform_define = 'PLATFORM_'.uc($build_os);

	# blank extra flags by default
	$platform_compile_line_extra = '';
	$platform_link_line_extra = '';
	
	# compiler version?
	$gcc_v3 = 0;
	$gcc_v4 = 0;
	open COMPILER,"gcc -v 2>&1 |" or die "Can't open gcc -v";
	while(<COMPILER>)
	{
		$gcc_v3 = 1 if (m/version gcc 3/ || m/gcc version 3/ || m/gcc \(GCC\) 3/i || m/gcc.Version\s+3/i);
		$gcc_v4 = 1 if (m/version gcc 4/ || m/gcc version 4/ || m/gcc \(GCC\) 4/i || m/gcc.Version\s+4/i);
	}
	close COMPILER;
	$gcc_v3 = 1 if $gcc_v4;		# hacks are about the same

	# get version
	open VERSION,"VERSION.txt";
	$product_version = <VERSION>;
	chomp $product_version;
	$product_name = <VERSION>;
	chomp $product_name;
	close VERSION;
	
	# where to put the files
	$install_into_dir = '/usr/local/bin';
	
	# if it's Darwin,
	if($build_os eq 'Darwin')
	{
		# see how many processors there are, and set make flags accordingly
		my $cpus = `sysctl hw.ncpu`;
		if($cpus =~ m/hw.ncpu:\s(\d+)/ && $1 > 1)
		{
			print "$1 processors detected, will set make to perform concurrent jobs\n";
			$sub_make_options = ' -j '.($1 + 1);
		}
		
		# test for fink installation
		if(-d '/sw/include' && -d '/sw/lib')
		{
			print "Fink installation detected, will use headers and libraries\n";
			$platform_compile_line_extra = '-I/sw/include ';
			$platform_link_line_extra = '-L/sw/lib ';
		}
	}
}

sub make_flag
{
	if($bsd_make)
	{
		return "-D $_[0]"
	}
	return $_[0].'=1';
}

1;

