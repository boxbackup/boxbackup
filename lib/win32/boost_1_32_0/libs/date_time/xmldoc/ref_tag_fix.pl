#!/usr/bin/perl -w
# Bart Garst - 7/1/2004 
# additional comments at bottom of file

#############################################################################
#  Copyright (c) 2001-2004 CrystalClear Software, Inc.                      #
#  Subject to the Boost Software License, Version 1.0.                      #
#  (See accompanying file LICENSE-1.0 or  http://www.boost.org/LICENSE-1.0) #
#############################################################################

use strict;

# key-value of file name and id attribute
# if the attributes are changed here it would be a good idea to
# also change the links in doxy.xml
my %files = (
  'date_time_autodoc.boostbook'  => 'date_time_reference',
  'gregorian_autodoc.boostbook'  => 'gregorian_reference',
  'posix_time_autodoc.boostbook' => 'posix_time_reference'
);


foreach my $key(keys %files) {
  rewrite_tags($key, $files{$key});
}

exit;

### subroutines ###

# separate words at underscores and capitalize first letter of each
sub make_title {
  my $a = shift || die "Missing required parameter to make_title()\n";
  my @wrds = split(/_/, $a); # remove underscores
  foreach(@wrds){
    $_ = "\u$_"; # capitalize first letter
  }
  $a = join(" ",@wrds);

  return $a;
}

sub rewrite_tags {
  my $filename = shift || die "Error: argument 1 missing to sub $!";
  my $id_tag = shift || die "Error: argument 2 missing to sub $!";
  my ($line, @new_file, $title);
  
  print "...processing $filename...\n";

  # prepare a title from id attribute
  $title = make_title($id_tag);
  
  # open & read file and change appropriate line
  open(INP, "<$filename") || die "File open (read) failed: $!";
  while($line = <INP>){
    if($line =~ /<library-reference>/) {
      push(@new_file, "<section id=\"$id_tag\">\n");
      push(@new_file, "<title>$title</title>\n");
    }
    elsif($line =~ /<\/library-reference>/) {
      push(@new_file, "</section>\n");
    }
    else{
      push(@new_file, $line);
    }
  }
  close(INP);
  
  # open & write new file w/ same name
  open(OTP, ">$filename") || die "File open (write) failed: $!";
  print OTP shift(@new_file);

  print OTP <<EO_LIC;
<!-- Copyright (c) 2001-2004 CrystalClear Software, Inc.
     Subject to the Boost Software License, Version 1.0. 
     (See accompanying file LICENSE-1.0 or  http://www.boost.org/LICENSE-1.0)
-->
EO_LIC

  foreach(@new_file){
    print OTP "$_";
  }
  close(OTP);
}

__END__

Rewrites the library-reference tagset as a section tagset and adds
a title to the generated *.boostbook files. It will NOT update a 
file that has already been rewritten.

Change log
7/19/2004
        - rewrite library-reference tags as section tags and add title tags
        - renamed fix_id sub to rewrite_tags.
8/31/2004
        - added license to this file and writes license to boostbook files
        
