package WebAppFramework::Unit::FormItem::SubmitButton;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;

# new() parameters:
#	Name => Form item name
#	Text => Value text (will be translated)
#	Report => if present, the clicked status of the button will be reported

# To change the text of the label, put a Unit in the 'Label' position.
# But be careful it only outputs text!

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# write the item
		$output->write_text('<input type="submit" name="'.$self->get_id().'"');
		if(exists ${$$self{'_units'}}{'Label'})
		{
			# write this unit as a label
			my $label = ${$$self{'_units'}}{'Label'};
			$output->write_text(' value="');
			$label->write_unit($output,$phase,$subphase);
			$output->write_text('"');
		}
		elsif(exists $$self{'Text'})
		{
			# output normal static text
			$output->write_text(' value="');
			$output->write_text_translated($$self{'Text'});
			$output->write_text('"');
		}
		$output->write_text('>');
	}
	else
	{
		# write stuff for label unit, if it exists
		if(exists ${$$self{'_units'}}{'Label'})
		{
			my $label = ${$$self{'_units'}}{'Label'};
			$label->write_unit($output,$phase,$subphase);
		}
	}
}

# by default, no form variable is used for submit buttons
# If the result should be reported, then the name followed by 'Clicked' is reported
sub get_form_variables
{
	my ($self) = @_;

	return () unless exists $$self{'Report'};

	return (cppvar('bool', $$self{'Name'}.'Clicked'))
}

# and no acceptor code should be written, unless we're reporting the clickedness
sub requires_acceptor_code()
{
	my ($self) = @_;
	return exists $$self{'Report'};
}

# report acceptance?
sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name) = @_;
	
	return unless exists $$self{'Report'};
	
	$output->write_code('m'.$self->get_item_name()."Clicked = true;\n");
	die "Internal logic error" unless $validity_error_name eq '';
}

# shouldn't ever display an error marker
sub disable_error_marker
{
	return 1;
}

# doesn't do any validation
sub always_passes_validation
{
	return 1
}

1;

