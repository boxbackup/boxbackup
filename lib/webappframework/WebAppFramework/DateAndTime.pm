package WebAppFramework::DateAndTime;
use strict;

# This module provides utility functions relating to date and time


# make_YMD_params($unit_ref, @standard_specification)
#
# Takes a date specification, and turns it into a year, month, day arguments.
#
sub make_YMD_params_array
{
	my ($unit, $type, @args) = @_;
	
	if($type eq 'ymd')
	{
		die "'ymd' specifier for dates doesn't have the correct number of arguments"
			unless $#args == 2;
		return map {$unit->get_variable($_)->convert_to('int32_t')} @args;
	}
	elsif($type eq 'WAFFormItemDate')
	{
		my $v = $unit->get_variable($args[0])->convert_to('WAFFormItemDate');
		return ("$v.GetYear()", "$v.GetMonth()", "$v.GetDay()");
	}
	else
	{
		die "Unknown date format specifier '$type'"
	}
}
sub make_YMD_params
{
	join(', ',make_YMD_params_array(@_))
}


1;
