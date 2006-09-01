package WebAppFramework::Unit::RawHTML;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to output untranslated HTML

# new() parameters:
#	HTML => raw HTML to output

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# write the HTML, interpolating in variables
		$self->write_interpolated_text($output, $$self{'HTML'});
	}
}

1;