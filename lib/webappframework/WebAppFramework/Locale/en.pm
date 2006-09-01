package WebAppFramework::Locale::en;
use strict;
use base 'WebAppFramework::Locale';
use vars '%_information';

%_information = (
	# text translations
	'Day' => 'Day',
	'Month' => 'Month',
	'Year' => 'Year',
	
	# Note: Preferred coding style for accessing lists of months etc is to look them up
	# using the C++ object at runtime.
	'MonthList' => 'January|February|March|April|May|June|July|August|September|October|November|December',
	'DayList' => 'Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday',
	
	# other information
	'FormItemDateOrdering' => 'DMY',	# how fields in a date form item are ordered
);

sub get
{
	my ($self,$item) = @_;
	if(exists $_information{$item})
	{
		return $_information{$item}
	}
	else
	{
		die "'$item' is an unrecognised locale item"
	}
}

1;

