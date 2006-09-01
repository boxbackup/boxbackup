package WebAppFramework::Unit::ChoiceLookupList;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::ChoiceLookup';

# Unit to display a list of elements from a translated list using a page variable as the index.

# new() parameters:
#	Variable => name of variable (resolving to a string, containing a ',' separated list of integers)
#	Choices => list for translation, | separated
#	Separator => separator (optional, not translated)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# separator
		my ($separator1,$separator2) = ('','');
		if(exists $$self{'Separator'})
		{
			my $separator = $$self{'Separator'};
			$separator1 = "\nbool first = true;";
			$separator2 = "\nif(first)\n{\nfirst = false;\n}\nelse\n{\nrResponse.Write(".
					WebAppFramework::Output::string_to_cpp_static_string($separator)
					.', '.(length($separator)).");\n}";
		}
	
		# get variable, and make available to the code.
		my $var = $self->get_variable($$self{'Variable'});
		$output->write_code("{\nstd::string _CLL_list(".$var->convert_to('std::string').");\n");
		$output->write_code(<<__E);
			const char *ptr = _CLL_list.c_str();
			char *endptr = 0;
			int32_t value = 0;$separator1
			while((value = ::strtol(ptr, &endptr, 0)), endptr != 0 && endptr != ptr)
			{$separator2
__E
		$self->write_choice_display($output, 'value');
		$output->write_code(<<__E);
				// end of list?
				if(*endptr != ',') break;
				// next
				ptr = endptr + 1;
			}
		}
__E
	}
}

# need to include the library functions
sub get_required_headers_this
{
	my ($self, $type) = @_;
	if($type == WebAppFramework::Unit::HEADERS_SYSTEM)
	{
		return ('stdlib.h','limits.h')
	}
	return ()
}



1;
