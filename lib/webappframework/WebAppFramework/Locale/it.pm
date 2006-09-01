package WebAppFramework::Locale::it;
use strict;
use base 'WebAppFramework::Locale';
use vars '%_information';

%_information = (
	# text translations
	'Day' => 'Giorno',
	'Month' => 'Mese',
	'Year' => 'Anno',
	
	# Note: Preferred coding style for accessing lists of months etc is to look them up
	# using the C++ object at runtime.
	'MonthList' => 'Gennaio|Febbraio|Marzo|Aprile|Maggio|Giugno|Luglio|Agosto|Settembre|Ottobre|Novembre|Dicembre',
	'DayList' => 'Lunedì|Martedì|Mercoledì|Giovedì|Venerdì|Sabato|Domenica',

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

