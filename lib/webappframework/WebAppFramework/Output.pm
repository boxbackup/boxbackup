package WebAppFramework::Output;
use strict;
use WebAppFramework::Unit;
use WebAppFramework::LanguageFile;
use vars qw/$_last_output_object_created/;

sub new
{
	my ($type, $filehandle, $page_identifier) = @_;
	my $self = {};
	$$self{'filehandle'} = $filehandle;
	$$self{'text_var'} = '_WebAppFrameworkText_'.$page_identifier;
	$$self{'indent_level'} = 0;
	bless $self;
	# store so the global find_output function can find it...
	$_last_output_object_created = $self;
	$self
}

# This function returns the last Output object created.
# Use as an absolute last resort.
sub find_output_object
{
	return $_last_output_object_created;
}

sub DESTROY
{
	my ($self) = @_;

	# make sure that if the language files are not saved, there is at least a big warning printed
	if(exists $$self{'language_file'} || exists $$self{'default_language_file'})
	{
		print "\n\nWARNING: Output object destroyed without saving updated language files\n\n\n"
	}
}

sub setup_languages
{
	my ($self, $language, $default_language, $current_page) = @_;
	$$self{'language'} = $language;
	$$self{'default_language'} = $default_language;
	
	if($language eq 'COMMON')
	{
		# no translation for the COMMOM file
		return;
	}
	
	# load the default language
	my $lang = undef;
	my $default_lang = undef;
	
	if(!-e 'Languages/default.txt')
	{
		print "WARNING: Creating empty language file for default language\n";
		$lang = WebAppFramework::LanguageFile->new('Languages/default.txt', $current_page, undef);
	}
	else
	{
		# if the language is being loaded to be used as the default language, don't
		# suppress the usage for the current page, otherwise the default language
		# loses all it's usage lists.
		$lang = WebAppFramework::LanguageFile->load('Languages/default.txt', $current_page, undef, ($language ne $default_language));
	}

	# load the language file
	if($language ne $default_language)
	{
		$default_lang = $lang;
		$lang = undef;
		
		my $lang_file = 'Languages/'.$language.'.txt';
		if(!-e $lang_file)
		{
			print "WARNING: Creating empty language file for language $language ($lang_file)\n";
			$lang = WebAppFramework::LanguageFile->new($lang_file, $current_page, $default_lang);
		}
		else
		{
			$lang = WebAppFramework::LanguageFile->load($lang_file, $current_page, $default_lang);
		}
	}
	
	# Store
	$$self{'language_file'} = $lang;
	$$self{'default_language_file'} = $default_lang if defined $default_lang;
}

sub get_language
{
	my ($self) = @_;
	return $$self{'language'};
}

sub get_default_language
{
	my ($self) = @_;
	return $$self{'default_language'};
}

# Register a code processor. This object will be able to modify any code
# which is written using write_code(). It must implement preprocess_code()
# which takes a two arguments, the first of this output object, and the
# second of the code to preprocess, and returns the code to write.
sub set_code_preprocessor
{
	my ($self,$preprocess) = @_;
	$$self{'preprocess_code'} = $preprocess;
}

sub write_code
{
	my ($self, $code) = @_;

	$self->flush_text();

	my $f = $$self{'filehandle'};
	
	# pre-process code?
	if(exists $$self{'preprocess_code'})
	{
		$code = $$self{'preprocess_code'}->preprocess_code($code, $self);
	}
	
	# split up and do auto-indenting
	my @lines = split /(\n+)/,$code;
	for my $l (@lines)
	{
		$l =~ s/\A\t+//;
		if($l =~ m/[^\n]/s)
		{
			# something other than just blank lines is present
			my $this_line_indent = $$self{'indent_level'};
			# do level handling
			my $t = $l;
			# remove strings and comments
			$t =~ s`\\(\\|n|t|")`*`;
			$t =~ s`//.*\Z``;
			$t =~ s`"[^"]"``;
			my $balanced = 0;
			while($t =~ m/({|})/g)
			{
				if($1 eq '{')
				{
					$$self{'indent_level'} ++;
					$balanced ++;
				}
				else
				{
					$$self{'indent_level'} --;
					$balanced --;
				}
			}
			# fudge for case and default labels
			$this_line_indent -- if $balanced < 0;
			$this_line_indent -- if $l =~ m/\A\s*(case|default|public|private|protected)/;
			# write out the line at an appropraite indent
			print $f "\t" x $this_line_indent, $l;
		}
		else
		{
			print $f $l
		}
	}
	
#	print $f $code;
}

sub write_text
{
	my ($self, $text) = @_;

	# get rid of bad characters
	$text =~ tr/\r//d;
	
	$$self{'text'} .= $text;
}

sub write_text_translated
{
	my ($self, $text) = @_;

	# check
	die "In Output, setup_languages not called, or attempting to use translation in COMMON files"
		unless exists $$self{'language_file'};

	# route via language
	$self->write_text($$self{'language_file'}->translate_and_update($text));
}

sub write_text_translate_within_markers
{
	my ($self, $text) = @_;

	# translate things within the HTML markers
	$self->translate_within_markers(\$text);
	
	# write the text
	$self->write_text($text);
}

# note: first arg is a reference
sub translate_within_markers
{
	my ($self, $text_r) = @_;
	die "Output::translate_within_markers must be passed a reference to alter" unless ref($text_r);
	
	# use a search and replace to adjust the text
	$$text_r =~ s/\Q<!--T-->\E(.*?)\Q<!--T-->\E/$$self{'language_file'}->translate_and_update($1)/ges;
}

sub translate_text
{
	my ($self, $text) = @_;
	
	# check
	die "In Output, setup_languages not called, or attempting to use translation in COMMON files"
		unless exists $$self{'language_file'};

	# route via language
	return $$self{'language_file'}->translate_and_update($text);
}

sub flush_text
{
	my ($self) = @_;
	
	my $text = $$self{'text'};
	my $text_len = length($text);
	
	if($text ne '')
	{
		# the text
		my $f = $$self{'filehandle'};
		print $f "\t" x $$self{'indent_level'},"rResponse.Write(",string_to_cpp_static_string($text, $$self{'indent_level'} + 2),", $text_len);\n";
		
		# unset the stored text
		$$self{'text'} = '';
	}
}

sub string_to_cpp_static_string
{
	my ($string, $indent_level) = @_;
	$indent_level = 2 if $indent_level eq '';

	# not necessarily the most wonderful way of doing this, but...
	my @lines = split /(.{0,80})/s, $string;
	my $str = '';

	my $first_line = 1;
	for(my $l = 0; $l <= $#lines; $l++)
	{
		next if $lines[$l] eq '';
		
		# line start
		$str .= (($first_line)?'"':" \\\n".("\t" x $indent_level).'"');
		$first_line = 0;

		# Alter partial line to fit in a nice C string
		# WARNING: Must not change the size of the string by these transforms!
		my $t = $lines[$l];
		$t =~ s/\\/\\\\/g;
		$t =~ s/\n/\\n/g;
		$t =~ s/\t/\\t/g;
		$t =~ s/"/\\"/g;

		# output	
		$str .= $t.'"';	
	}

	$str
}

sub save_language_files
{
	my ($self) = @_;

	if(exists $$self{'language_file'})
	{
		$$self{'language_file'}->save();
		delete $$self{'language_file'};
	}
	if(exists $$self{'default_language_file'})
	{
		$$self{'default_language_file'}->save();
		delete $$self{'default_language_file'};
	}
}


1;
