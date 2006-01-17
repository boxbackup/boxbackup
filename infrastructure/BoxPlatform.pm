# distribution boxbackup-0.09
# 
#  
# Copyright (c) 2003, 2004
#      Ben Summers.  All rights reserved.
#  
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All use of this software and associated advertising materials must 
#    display the following acknowledgement:
#        This product includes software developed by Ben Summers.
# 4. The names of the Authors may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
# 
# [Where legally impermissible the Authors do not disclaim liability for 
# direct physical injury or death caused solely by defects in the software 
# unless it is modified by a third party.]
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#  
#  
#  
package BoxPlatform;
use Exporter;
@ISA = qw/Exporter/;
@EXPORT = qw/$build_os $make_command $bsd_make $platform_define $gcc_v3 $product_version $product_name $install_into_dir $sub_make_options $platform_compile_line_extra $platform_link_line_extra/;

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
	open COMPILER,"gcc -v 2>&1 |" or die "Can't open gcc -v";
	while(<COMPILER>)
	{
		$gcc_v3 = 1 if (m/version gcc 3/ || m/gcc version 3/ || m/gcc \(GCC\) 3/i || m/gcc.Version\s+3/i);
	}
	close COMPILER;

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

