#!/usr/bin/perl -w
use strict;

open (EXCEPT, "<../../ExceptionCodes.txt") or die "Can't open ../../ExceptionCodes.txt: $!\n";
open (DOCBOOK, ">ExceptionCodes.xml") or die "Can't open Exceptioncodes.xml for writing: $!\n";

print DOCBOOK <<EOD;
<?xml version="1.0" encoding="UTF-8"?>

<appendix>
    <title>Exception codes</title>

EOD
my $sectionName;
my $sectionNum;
my $sectionDesc;
my $exceptionCode;
my $exceptionShortDesc;
my $exceptionLongDesc;
while(<EXCEPT>)
{
    next if(m/^#/);
    chomp;
    if(m/^EXCEPTION TYPE (\w+) (\d+)/)
    {
        $sectionName = ucfirst(lc($1));
        $sectionNum = $2;
        if($sectionName ne "Common")
        {
            $sectionDesc = "the " . $sectionName;
        }
        else
        {
            $sectionDesc = "any";
        }
        print DOCBOOK <<EOD;
    <section>
      <title>$sectionName Exceptions ($sectionNum)</title>
      
      <para>These are exceptions that can occur in $sectionDesc module
      of the system.</para>
      
      <itemizedlist>
EOD
    }
    
    # The END TYPE line
    if(m/^END TYPE$/)
    {
        print DOCBOOK "      </itemizedlist>\n    </section>\n";
    }
    
    # The actual exceptions
    if(m/(\(\d+\/\d+\)) - (\w+ \w+)(?: - )?(.*)$/)
    {
        $exceptionCode = $1;
        $exceptionShortDesc = $2;
        $exceptionLongDesc = $3;
        
        print DOCBOOK "        <listitem>\n          <para><emphasis role=\"bold\">";
        print DOCBOOK $exceptionCode . ": " . $exceptionShortDesc . "</emphasis>";
        if($exceptionLongDesc ne "")
        {
            print DOCBOOK " -- " . $exceptionLongDesc;
        }
        print DOCBOOK "</para>\n        </listitem>\n";
    }
}

print DOCBOOK "</appendix>\n";
        