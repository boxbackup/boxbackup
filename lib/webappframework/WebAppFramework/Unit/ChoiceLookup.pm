package WebAppFramework::Unit::ChoiceLookup;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit to display an element from a translated list using a page variable as the index.

# new() parameters:
#	Variable => name of variable
#	Choices => list for translation, | separated

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		my $var = $self->get_variable($$self{'Variable'});
		$output->write_code("{\nint32_t value = ".$var->convert_to('int32_t').";\n");
		$self->write_choice_display($output, 'value');
		$output->write_code("}\n");
	}
}

# writes the code to display the actual list item
sub write_choice_display
{
	my ($self, $output, $localvar) = @_;
	
	# translate the list of choices and split into elements
	my $choicelist = $output->translate_text($$self{'Choices'});
	my @choices = split /\|/,$choicelist;
	
	# write code...
	my $list_len = ($#choices + 1);
	$output->write_code("static const char *lookup[] = {\n");
	$output->write_code(join(', ',map {WebAppFramework::Output::string_to_cpp_static_string($_)} @choices)."\n");
	$output->write_code("};\n");
	$output->write_code(<<__E);
			if($localvar < 0 || $localvar >= $list_len)
			{
				// Bad value
				rResponse.Write("?",1);
			}
			else
			{
				rResponse.WriteString(lookup[value]);
			}
__E
}

1;
