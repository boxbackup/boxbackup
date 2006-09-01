package WebAppFramework::Unit::Localised::Date;
use strict;
use base 'WebAppFramework::Unit';
use WebAppFramework::DateAndTime;

# Unit to output a date via the locale mechanism

# new() parameters:
#	Date => standard date specification
#	Style => Long or Short (defaults to Long)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# style of date output?
		my $style = $$self{'Style'};
		$style = 'Long' unless $style ne '';
		
		# Get parameters
		my $params = WebAppFramework::DateAndTime::make_YMD_params(
				$self,
				$self->list_to_array($$self{'Date'})
			);
	
		# write the code!
		$output->write_code(<<__E);
			{
				std::string formattedDate;
				locale.FormatDate(WAFLocale::DateFormat$style, $params, formattedDate);
				rResponse.Write(formattedDate.c_str(), formattedDate.size());
			}
__E
	}
}


1;

