package WebAppFramework::Unit::DataSource;
use strict;
use base 'WebAppFramework::Unit';


# Is the key (ie value="" attribute) an integer? 
sub key_is_integer
{
	# default is an integer, it's nicer to store
	return 1;
}

# Is the string data trusted to be free of nasty things?
sub string_is_trusted
{
	# default is not to trust
	return 0;
}

# Write the code
# Args: ref to function to write an output, output, phase, subphase
#			function takes args: key_source, string_souce, output, phase, subphase
sub write_bound_item
{
	my ($self, $function, $output, $phase, $subphase) = @_;
	
	# default does nothing
}

# Get the maximum possible range of an integer result.
# Return (min,max) as an array, either undefined to omit the test for that bound
sub get_integer_range
{
	return ()
}

1;
