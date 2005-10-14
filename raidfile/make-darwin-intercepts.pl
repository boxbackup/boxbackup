#!/usr/bin/perl
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

