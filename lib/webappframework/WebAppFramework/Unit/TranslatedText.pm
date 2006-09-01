package WebAppFramework::Unit::TranslatedText;
use strict;
use base 'WebAppFramework::Unit';

# new() parameters:
#	Text => text to output, after translation

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
	
	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# translate the text
		my $translated = $output->translate_text($$self{'Text'});

		# Write text, interpolated
		$self->write_interpolated_text($output, $translated);
	}
}

1;
