#!/usr/bin/perl
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
use strict;

my $out = $ARGV[0];
die "No out directory specified" unless $out ne '';

my @calls = split /[\r\n]+/,<<__E;
lseek SYSCALL_TEST(lseek, 3)
open SYSCALL_TEST(open, 3)
close SYSCALL_TEST(close, 1)
write SYSCALL_TEST(write, 3)
read SYSCALL_TEST(read, 3)
readv SYSCALL_TEST(readv, 3)
__E

for(@calls)
{
	my ($name,$line) = split / /,$_,2;
	
	open FL,">$out/intercept-$name.s" or die "Can't open out file";
	print FL <<'__S';
#include "../../../test/raidfile/Darwin-SYS.h"

#define	SYSCALL_TEST(name, nargs)			\
	.globl	cerror				@\
LEAF(_TEST_##name)					@\
	kernel_trap_args_##nargs		@\
	li	r0,SYS_##name			@\
	sc					@\
	b	1f   				@\
	b	2f				@\
1:	BRANCH_EXTERN(cerror)			@\
.text						\
2:	nop

__S
	print FL $line,"\n\tblr\n\n";

	close FL;
	
	if(system("gcc -c $out/intercept-$name.s -o $out/intercept-$name.o") != 0)
	{
		die "Assembly failed\n";
	}
}

