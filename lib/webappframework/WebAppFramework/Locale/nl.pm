package WebAppFramework::Locale::nl;
use strict;
use base 'WebAppFramework::Locale';
use vars '%_information';

%_information = (
	# text translations
	'Day' => 'dag',
	'Month' => 'maand',
	'Year' => 'jaar',
	
 	# NOTE: Dutch people do not capitalise their month/day names
	
	# Note: Preferred coding style for accessing lists of months etc is to look them up
	# using the C++ object at runtime.
	'MonthList' => 'januari|februari|maart|april|mei|juni|juli|augustus|september|oktober|november|december',
	'DayList' => 'maandag|dinsdag|woensdag|donderdag|vrijdag|zaterdag|zondag',
	
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

