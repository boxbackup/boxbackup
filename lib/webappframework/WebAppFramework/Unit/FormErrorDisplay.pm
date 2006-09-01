package WebAppFramework::Unit::FormErrorDisplay;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Arguments to new()
#	Form => reference to form

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# set var in form to stop it doing stuff itself
		${$$self{'Form'}}{'_externally_displayed_errors'} = 1;
	}

	# Ask the form to write the display here
	$$self{'Form'}->write_error_display($output, $phase, $subphase);
}


1;

