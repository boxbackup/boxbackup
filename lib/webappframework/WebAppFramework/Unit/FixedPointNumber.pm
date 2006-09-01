package WebAppFramework::Unit::FixedPointNumber;
use strict;
use base 'WebAppFramework::Unit';
use WebAppFramework::FixedPoint;

# Unit to output a fixed point number

# new() parameters:
#	Variable => name of variable
#	ScaleDigits => number of digits for (base 10) fixed point values
#	DisplayDigits => min number of digits to use for the fractional part. Defaults to ScaleDigits.

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# use utility function
		WebAppFramework::FixedPoint::write_fixed_point_value($output,
			$self->get_variable($$self{'Variable'}),
			$$self{'ScaleDigits'}, $$self{'DisplayDigits'});
	}
}

1;
