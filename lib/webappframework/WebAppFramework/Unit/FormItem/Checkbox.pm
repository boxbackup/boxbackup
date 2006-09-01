package WebAppFramework::Unit::FormItem::Checkbox;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;

# new() parameters:
#	Name => Form item name
#	Label => Value text (will be translated)
# 	Default => source of default value (can be CONSTANT:true/false for constant values)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# Generate attributes
		my $attr = ' type="checkbox" name="'.$self->get_id().'"';
		$attr .= $self->get_tag_attributes();

		# write the item
		if($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
		{
			$output->write_text("<input$attr");
			
			# get original default
			my $default;
			my $constant_default = undef;
			if(exists $$self{'Default'})
			{
				if($$self{'Default'} =~ m/\ACONSTANT:(.+?)\Z/)
				{
					my $c = $1;
					die "Checkbox FormItem constant default must be true or false"
						unless $c eq 'true' || $c eq 'false';
					$default = cppvar('bool', $1);
					$constant_default = ($c eq 'true');
				}
				else
				{
					$default = $self->get_variable($$self{'Default'})
				}
			}
			else
			{
				$default = cppvar('bool', 'false');
				$constant_default = 0;
			}
			# filter default
			{
				my $d = $self->filter_default_value($default, 'bool');
				if($d != $default)
				{
					$constant_default = undef;
					$default = $d;
				}
			}
			
			# write default value
			if(defined $default)
			{			
				if(defined $constant_default)
				{
					if($constant_default)
					{
						$output->write_text(" checked");
					}
				}
				else
				{
					$output->write_code("if(".$default->convert_to('bool').")\n{\n");
					$output->write_text(" checked");
					$output->write_code("}\n");
				}
			}
		}
		else
		{
			# is item already checked
			$output->write_text("<input$attr");
			my $v = $self->get_variable($self->get_value_page_variable());
			$output->write_code("if(".$v->convert_to('bool').")\n{\n");
			$output->write_text(" checked");
			$output->write_code("}\n");
		}
		$output->write_text(">");
		$output->write_text_translated($$self{'Label'});
		$output->write_text("</input>");
	}
}

# Field is always a bool
sub get_form_variable_type
{
	my ($self) = @_;
	return ('bool')
}

sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name) = @_;
	
	$output->write_code('m'.$self->get_item_name()." = true;\n");
	die "Internal logic error" unless $validity_error_name eq '';
}

# check boxes can't be validated
sub always_passes_validation
{
	my ($self) = @_;
	return 1;
}

1;
