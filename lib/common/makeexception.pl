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

# global exception list file
my $global_list = '../../ExceptionCodes.txt';


my @exception;
my @exception_desc;
my $class;
my $class_number;

# read the description!

open EXCEPTION_DESC,$ARGV[0] or die "Can't open $ARGV[0]";

while(<EXCEPTION_DESC>)
{
	chomp; s/\A\s+//; s/#.+\Z//; s/\s+\Z//; s/\s+/ /g;
	next unless m/\S/;

	if(m/\AEXCEPTION\s+(.+)\s+(\d+)\Z/)
	{
		$class = $1;
		$class_number = $2;
	}
	else
	{
		my ($name,$number,$description) = split /\s+/,$_,3;
		if($name eq '' || $number =~ m/\D/)
		{
			die "Bad line '$_'";
		}
		if($exception[$number] ne '')
		{
			die "Duplicate exception number $number";
		}
		$exception[$number] = $name;
		$exception_desc[$number] = $description;
	}
}

die "Exception class and number not specified" unless $class ne '' && $class_number ne '';

close EXCEPTION_DESC;

# write the code
print "Generating $class exception...\n";

open CPP,">autogen_${class}Exception.cpp" or die "Can't open cpp file for writing";
open H,">autogen_${class}Exception.h" or die "Can't open h file for writing";

# write header file
my $guardname = uc 'AUTOGEN_'.$class.'EXCEPTION_H';
print H <<__E;

// Auto-generated file -- do not edit

#ifndef $guardname
#define $guardname

#include "BoxException.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ${class}Exception
//		Purpose: Exception
//		Created: autogen
//
// --------------------------------------------------------------------------
class ${class}Exception : public BoxException
{
public:
	${class}Exception(unsigned int SubType)
		: mSubType(SubType)
	{
	}
	
	${class}Exception(const ${class}Exception &rToCopy)
		: mSubType(rToCopy.mSubType)
	{
	}
	
	~${class}Exception() throw ()
	{
	}
	
	enum
	{
		ExceptionType = $class_number
	};

	enum
	{
__E

for(my $e = 0; $e <= $#exception; $e++)
{
	if($exception[$e] ne '')
	{
		print H "\t\t".$exception[$e].' = '.$e.(($e==$#exception)?'':',')."\n"
	}
}

print H <<__E;
	};
	
	virtual unsigned int GetType() const throw();
	virtual unsigned int GetSubType() const throw();
	virtual const char *what() const throw();

private:
	unsigned int mSubType;
};

#endif // $guardname
__E

# -----------------------------------------------------------------------------------------------------------

print CPP <<__E;

// Auto-generated file -- do not edit

#include "Box.h"
#include "autogen_${class}Exception.h"

#include "MemLeakFindOn.h"

#ifdef EXCEPTION_CODENAMES_EXTENDED
	#ifdef EXCEPTION_CODENAMES_EXTENDED_WITH_DESCRIPTION
static const char *whats[] = {
__E

my $last_seen = -1;
for(my $e = 0; $e <= $#exception; $e++)
{
	if($exception[$e] ne '')
	{
		for(my $s = $last_seen + 1; $s < $e; $s++)
		{
			print CPP "\t\"UNUSED\",\n"
		}
		my $ext = ($exception_desc[$e] ne '')?" ($exception_desc[$e])":'';
		print CPP "\t\"${class} ".$exception[$e].$ext.'"'.(($e==$#exception)?'':',')."\n";
		$last_seen = $e;
	}
}

print CPP <<__E;
};
	#else
static const char *whats[] = {
__E

$last_seen = -1;
for(my $e = 0; $e <= $#exception; $e++)
{
	if($exception[$e] ne '')
	{
		for(my $s = $last_seen + 1; $s < $e; $s++)
		{
			print CPP "\t\"UNUSED\",\n"
		}
		print CPP "\t\"${class} ".$exception[$e].'"'.(($e==$#exception)?'':',')."\n";
		$last_seen = $e;
	}
}

print CPP <<__E;
};
	#endif
#endif

unsigned int ${class}Exception::GetType() const throw()
{
	return ${class}Exception::ExceptionType;
}

unsigned int ${class}Exception::GetSubType() const throw()
{
	return mSubType;
}

const char *${class}Exception::what() const throw()
{
#ifdef EXCEPTION_CODENAMES_EXTENDED
	if(mSubType < 0 || mSubType > (sizeof(whats) / sizeof(whats[0])))
	{
		return "${class}";
	}
	return whats[mSubType];
#else
	return "${class}";
#endif
}

__E

close H;
close CPP;

# update the global exception list
my $list_before;
my $list_after;
my $is_after = 0;
if(open CURRENT,$global_list)
{
	while(<CURRENT>)
	{
		next if m/\A#/;
		
		if(m/\AEXCEPTION TYPE (\w+) (\d+)/)
		{
			# check that the number isn't being reused
			if($2 == $class_number && $1 ne $class)
			{
				die "Class number $class_number is being used by $class and $1 -- correct this.\n";
			}
			if($2 > $class_number)
			{
				# This class comes after the current one (ensures numerical ordering)
				$is_after = 1;
			}
			if($1 eq $class)
			{
				# skip this entry
				while(<CURRENT>)
				{
					last if m/\AEND TYPE/;
				}
				$_ = '';
			}
		}
		
		if($is_after)
		{
			$list_after .= $_;
		}
		else
		{
			$list_before .= $_;
		}
	}

	close CURRENT;
}

open GLOBAL,">$global_list" or die "Can't open global exception code listing for writing";

print GLOBAL <<__E;
#
# automatically generated file, do not edit.
#
# This file lists all the exception codes used by the system.
# Use to look up more detailed descriptions of meanings of errors.
#
__E

print GLOBAL $list_before;

print GLOBAL "EXCEPTION TYPE $class $class_number\n";
for(my $e = 0; $e <= $#exception; $e++)
{
	if($exception[$e] ne '')
	{
		my $ext = ($exception_desc[$e] ne '')?" - $exception_desc[$e]":'';
		print GLOBAL "($class_number/$e) - ${class} ".$exception[$e].$ext."\n";
	}
}
print GLOBAL "END TYPE\n";

print GLOBAL $list_after;

close GLOBAL;


