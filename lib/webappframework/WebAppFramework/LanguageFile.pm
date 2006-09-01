package WebAppFramework::LanguageFile;
use strict;
use Symbol;
use Fcntl qw(LOCK_EX SEEK_SET SEEK_CUR);
use Digest::MD5 qw(md5_hex);

use constant FILEHANDLE => 0;
use constant CONTENTS => 1;
use constant LOOKUP => 2;
use constant CURRENT_PAGE => 3;
use constant DEFAULT_LANG => 4;

use constant KEY => 0;
use constant PAGES => 1;
use constant TRANSLATED => 2;
use constant ORIGINAL => 3;

sub new
{
	my ($type, $filename, $current_page, $default_language_object) = @_;

	my $self = [open_file($filename), [], {}, $current_page, $default_language_object];
	bless $self;
	$self
}

# When loading a language file, the current_page parameter is used to remove
# all mentions of a page from the usage list on loading, and for the 
# update_and_translate method to update the usage list. This ensures that
# the usages lists remain accurate.
#
# The default language is used to obtain default translations. If a string
# is neither in this language or the default language, it will be added to both.
#
# A normal array with a hash array for lookup is used, rather than just a simple
# and more efficient hash array, so that the ordering of the file is maintained.
#
sub load
{
	my ($type, $filename, $current_page, $default_language_object, $dont_suppress_usage) = @_;

	# Read file into two arrays
	my $contents = [];
	my $lookup = {};
	
	# open file
	my $f = open_file($filename);
	# read contents
	my $key = '';
	my $in_pages = '';
	my $text = '';
	my $original = '';
	while(<$f>)
	{
		if(m/\A========\s*\Z/)
		{
			# remove extra space from the text
			$text =~ s/\A\n//;   # only the first newline
			$text =~ s/\n\n\Z//; # last two newlines
		
			# add entry
			if($key ne '')
			{
				my @used;
				if($dont_suppress_usage)
				{
					@used = split /\s+/,$in_pages
				}
				else
				{
					@used = map {($_ eq $current_page)?():$_} split /\s+/,$in_pages;
				}
				push @$contents,[$key, [@used], $text, $original];
				$$lookup{$key} = $#{$contents};
			}
			
			# reset
			$key = '';
			$in_pages = '';
			$text = '';
			$original = '';
		}
		elsif(m/\A#\s+(.+)\n?\Z/)
		{
			$key = $1
		}
		elsif(m/\A@\s+(.*)\n?\Z/)
		{
			$in_pages = $1
		}
		elsif(m/\A>\s?(.*)\n?\Z/)
		{
			$original .= "\n" unless $original eq '';
			$original .= $1
		}
		else
		{
			$text .= $_;
		}
	}

	# create object and return
	my $self = [$f, $contents, $lookup, $current_page, $default_language_object];
	bless $self;
	$self
}

sub open_file
{
	my ($filename) = @_;

	# open file
	my $f = gensym;
	if(-e $filename)
	{
		# open read/write
		open $f,'+<'.$filename or die "Can't open $filename for reading/writing language";
	}
	else
	{
		# open write only
		open $f,'>'.$filename or die "Can't open $filename for writing language";
	}
	# lock file
	flock($f,LOCK_EX);
	seek($f,0,SEEK_SET);
	
	# return the file handle
	$f;
}

sub translate_and_update
{
	my ($self, $source_text) = @_;
	
	# don't let things without any non-whitespace get into the translated files
	if($source_text !~ m/\S/)
	{
		return '';
	}

	# process the given text into the key and adjust the source text if necessary
	my $key;
	my $text;
	my $is_user_keyed = 0;	# whether the user specified the key
	if($source_text =~ m/\A\s*\*(.+?)\*(.*)\Z/s)
	{
		# text uses the *key*text notation
		$key = $1 . '-u';
		$text = $2;
		$is_user_keyed = 1;
	}
	elsif(length($source_text) > 32)
	{
		# long text, use a hash of the key
		$key = md5_hex($source_text) . '-h';
		$text = $source_text;
	}
	else
	{
		# short text, use a processed form of the key
		$key = $source_text;
		$key =~ s/\s+/_/gs;
		$key =~ s/(\A_|_\Z)//g;
		$key .= '-t';
		$text = $source_text;
	}

	# exists?
	if(exists ${$$self[LOOKUP]}{$key})
	{
		# yes.
		my $index = ${$$self[LOOKUP]}{$key};
		my $entry = ${$$self[CONTENTS]}[$index];
		
		# Update usage list
		my $u = 0;
		for(@{$$entry[PAGES]})
		{
			if($_ eq $$self[CURRENT_PAGE])
			{
				$u = 1;
				last
			}
		}
		unless($u)
		{
			push @{$$entry[PAGES]},$$self[CURRENT_PAGE]
		}

		if($is_user_keyed)
		{
			# update the original text
			$$entry[ORIGINAL] = $text;
			# if the default language, update the translated text too
			unless(defined $$self[DEFAULT_LANG])
			{
				$$entry[TRANSLATED] = $text;
			}
		}
		
		# return translation
		return $$entry[TRANSLATED]
	}
	
	# so, doesn't exist. The default translation is either the translation
	# from the default language, or the text itself. (This approach automatically
	# adds the string to the default language.)
	# NOTE: Use the source text when adding to the default language, so the
	# keys are maintained.
	my $default_translation = (defined $$self[DEFAULT_LANG])?($$self[DEFAULT_LANG]->translate_and_update($source_text)):$text;
	
	push @{$$self[CONTENTS]},[$key, [$$self[CURRENT_PAGE]], $default_translation, $text];
	$$self[LOOKUP]{$key} = $#{$$self[CONTENTS]};

	# and return that default
	$default_translation
}

sub save
{
	my ($self) = @_;

	# retrieve file handle, and seek to the beginning
	my $f = $$self[FILEHANDLE];
	seek($f,0,SEEK_SET);
	# truncate the file to zero bytes
	truncate $f,0;
	
	for(@{$$self[CONTENTS]})
	{
		my $o = $$_[ORIGINAL];
		$o =~ s/\s+\Z//;
		$o =~ s/^/> /mg;
		print $f '# ',
			$$_[KEY],"\n@ ",
			join (' ',sort(@{$$_[PAGES]})),"\n",
			$o,"\n\n",
			$$_[TRANSLATED],"\n\n========\n"
	}
	close $f
}

1;


