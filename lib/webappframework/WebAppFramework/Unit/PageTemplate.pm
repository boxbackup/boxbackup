package WebAppFramework::Unit::PageTemplate;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::TemplatedInsert';

# new() parameters:
#	Template => base name of template filename

sub remove_unwanted_text
{
	my ($self, $template_r) = @_;
	
	# remove bits of the page we're not supposed to include
	$$template_r =~ s/<!--PageTemplate-Omit-Begin-->(.+?)<!--PageTemplate-Omit-End-->//gs;
}

1;

