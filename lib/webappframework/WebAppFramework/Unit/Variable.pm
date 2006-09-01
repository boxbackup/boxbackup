package WebAppFramework::Unit::Variable;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to display a page variable (parameter, form entry, etc)

# new() parameters:
#	Variable => name of variable

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# use base class to write out variable
		$self->write_variable_text($output, $$self{'Variable'});
	}
}

1;
