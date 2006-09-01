package WebAppFramework::FixedPoint;
use strict;

# This module provides utility functions relating to fixed point numbers

sub write_fixed_point_value
{
	# Sort out the parameters
	my ($output,$value,$scaleDigits,$displayPlaces) = @_;
	$scaleDigits = int($scaleDigits);
	$displayPlaces = $scaleDigits unless defined $displayPlaces;
	$displayPlaces = int($displayPlaces);
	
	# convert the value to an integer
	my $v = $value->convert_to('int32_t');
	
	# write the display code
	$output->write_code(<<__E);
		{
			std::string formatted;
			WAFUtility::FixedPointDecimalToString($v, formatted, $scaleDigits, $displayPlaces);
			rResponse.Write(formatted.c_str(), formatted.size());
		}
__E
}

1;

