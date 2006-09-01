package WebAppFramework::Locale::jp;
use strict;
use base 'WebAppFramework::Locale';
use vars '%_information';

%_information = (
	# text translations
	'Day' => '日',
	'Month' => '月',
	'Year' => '年',
	
	# Note: Preferred coding style for accessing lists of months etc is to look them up
	# using the C++ object at runtime.
	'MonthList' => '1月|2月|3月|4月|5月|6月|7月|8月|9月|10月|11月|12月',
	'DayList' => '月曜日|火曜日|水曜日|木曜日|金曜日|土曜日|日曜日',

	# other information
	'FormItemDateOrdering' => 'YMD',	# how fields in a date form item are ordered
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

