package WebAppFramework::Unit::Localised::DateTime;
use strict;
use base 'WebAppFramework::Unit';
use WebAppFramework::DateAndTime;

# Unit to output a date via the locale mechanism

# new() parameters:
#	DateTime => Page variable
#	InFormat => YYYYMMDD or UNIXEpoch (defaults to UNIXEpoch if not specified)
#	Style => Any format specified in WAFLocale.h (defaults to DateTimeFormatLong if not specified)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		my $source = $self->get_variable($$self{'DateTime'})->convert_to('int32_t');
		my $style = 'DateTimeFormatLong';
		if(exists $$self{'Style'})
		{
			$style = $$self{'Style'}
		}
		my $informat = 'TimeFormatUNIXEpoch';
		if(exists $$self{'InFormat'})
		{
			$informat = 'TimeFormat'.$$self{'InFormat'}
		}
	
		# write the code!
		$output->write_code(<<__E);
			{
				int32_t dateTime = $source;
				std::string formattedDate;
				locale.FormatDateTime(WAFLocale::$style, WAFLocale::$informat, dateTime, formattedDate);
				rResponse.Write(formattedDate.c_str(), formattedDate.size());
			}
__E
	}
}


1;

