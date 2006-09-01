package WebAppFramework::Unit::SectionTemplate;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::TemplatedInsert';

# new() parameters:
#	Template => base name of template filename
#	Marker => Text used in HTML comments to mark the bit of text used

sub remove_unwanted_text
{
	my ($self, $template_r) = @_;
	
	# remove bits of the page we're not supposed to include
	my $marker = $$self{'Marker'};
	die "No Marker specified for SectionTemplate" unless $marker ne '';
	unless($$template_r =~ m/<!--\s*$marker\s*-->(.+?)<!--\s*$marker\s*-->/s)
	{
		die "Marker $marker not found in HTML comments in SectionTemplate"
	}
	$$template_r = $1;
}

1;

