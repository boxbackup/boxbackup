package WebAppFramework::Unit::FormItem;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use CppVariable;
use WebAppFramework::Unit::Form;

# new() parameters:
#	Name => Form item name
#	Label => Label, subject to translation. Used for automatically generating error messages
#	Validation => Validation information
#	ValidationFailMsg => Text to display when validation fails
#	Attributes => Extra tags for items, eg 'class="css_style"'

# override from base class
sub is_form_item
{
	return 1;
}

# most forms will just require one ID
sub get_num_ids_required
{
	return 1;
}

# called by the form with the required number of IDs as argument
sub set_ids
{
	my ($self, @ids) = @_;
	$$self{'_item_ids'} = [@ids];
}

# get entire set of IDs
sub get_ids
{
	my ($self) = @_;
	@{$$self{'_item_ids'}};
}

# in the case where there's just one ID, return the single ID
sub get_id
{
	my ($self) = @_;
	die "Form item ".$self->get_item_name()." has more than one ID" unless $self->get_num_ids_required() == 1;
	${$$self{'_item_ids'}}[0];
}

# return name of item
sub get_item_name
{
	my ($self) = @_;
	die "Form item has no name" unless exists $$self{'Name'};
	$$self{'Name'}
}

# get the label, abort if not present
sub get_item_label
{
	my ($self) = @_;
	die "Form item $$self{'Name'} (type ".ref($self).") has no label" unless exists $$self{'Label'};
	$$self{'Label'}
}

# get item validation string
sub get_item_validation
{
	my ($self) = @_;
	die "Form item $$self{'Name'} (type ".ref($self).") has no validation info" unless exists $$self{'Validation'};
	$$self{'Validation'}
}


# return a list of the variables used to return results
# The derived class must support get_form_variable_type(), which
# returns the C++ type of the variable.
sub get_form_variables
{
	my ($self) = @_;
	return (cppvar($self->get_form_variable_type(), $$self{'Name'}))
}

# whether or not this item requires form variable acceptor code
sub requires_acceptor_code()
{
	my ($self) = @_;
	return 1;
}

# return the form this item belongs in
sub get_form
{
	my ($self) = @_;
	return $self->get_parent_of_type('WebAppFramework::Unit::Form');
}

# Return a string containing any extra attributes for the class.
# First character is a space, for neat additions. Or the empty string if there are none.
sub get_tag_attributes
{
	my ($self) = @_;
	my $a = $$self{'Attributes'};
	$a =~ s/\s+/ /g;
	$a =~ s/\A\s+//;
	$a =~ s/\s+\Z//;
	($a eq '')?'':' '.$a
}

# Returns a page variable name referencing the current value entered by the user
sub get_value_page_variable
{
	my ($self) = @_;

	my @v = $self->get_form_variables();
	die "Item has more than one form variable, can't use get_value_page_variable()"
		unless $#v == 0;

	$self->get_form()->get_form_name().'.'.$v[0]->name();
}

# disable display of the error marker?
sub disable_error_marker
{
	return 0;
}

# override writing to handle writing error markers
sub write
{
	my ($self, $output, $phase, $subphase) = @_;

	# ask base class to do the magic
	$self->SUPER::write($output, $phase, $subphase);
	
	# only want to write the error marker in specific phase...
	return unless $phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT
		&& $subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_REDISPLAY;

	# is writing of error markers disabled?
	return if $self->disable_error_marker();
	
	# find the form
	my $form = $self->get_form();
	
	# get an error marker from somewhere
	my $error_marker = $self->find_form_template_fragment('ErrorMarker');
	if($self->always_passes_validation() || ($error_marker eq '' && !($form->inline_error_messages())))
	{
		# don't do anything if the null string is returned and error messages aren't inline
		# or if the field always passes validation
		return;
	}
	
	# ask the form what the expression is called
	my $expression = $form->get_is_item_valid_expression($self);
	
	# and write some code!
	$output->write_code("if(!$expression)\n{\n");
	$output->write_text($error_marker);
	if($form->inline_error_messages())
	{
		# write error message surrounded by fragments
		$output->write_text($self->find_form_template_fragment('InlineErrorStart'));
		$self->write_validation_fail_message($output);
		$output->write_text($self->find_form_template_fragment('InlineErrorEnd'));
	}
	$output->write_code("}\n");
}

# find an html fragment from the form or a container subclass.
# Should not be subjected to translation.
sub find_form_template_fragment
{
	my ($self, $frag_name) = @_;
	
	# Find a parent which can provide this -- search for a method
	# called get_form_template_fragment
	my $r = $self;
	while(exists $$r{'_parent'})
	{
		my $f = $$r{'_parent'}->get_form_template_fragment($frag_name);
		return $f if defined $f;
		$r = $$r{'_parent'};
	}

	die "FormItem::find_form_template_fragment could not locate an object implementing the required method"
}


# write the validation failed message
sub write_validation_fail_message
{
	my ($self, $output) = @_;
	
	if(exists $$self{'ValidationFailMsg'})
	{
		$output->write_text_translated($$self{'ValidationFailMsg'})
	}
	else
	{
		# How many possible messages are there?
		my @msg_vals = $self->get_values_of_validation_fail_messages();
		if($#msg_vals < 0)
		{
			# only only one message, just write that one
			$output->write_text_translated($self->make_validation_fail_message())
		}
		else
		{
			my $v = $self->get_form()->get_item_validity_error($self);
			# write a message for possible value
			for(@msg_vals)
			{
				my @vs = split /\|/,$_;
				$output->write_code("if(".join(' || ',map {"$v == $_"} @vs).")\n{\n");
				$output->write_text_translated($self->make_validation_fail_message($vs[0]));
				$output->write_code("}\n");
			}
		}
	}
}

# The constant values of possible validation failure messages
# Or the empty string, if there's just one
sub get_values_of_validation_fail_messages
{
	return ();
}

# doesn't always pass validation
sub always_passes_validation
{
	return 0
}

# what's the default state for the validation state variable?
sub default_validation_state
{
	return 'WebAppForm::NotValid'
}

# Filtering of default values
# This must be called by the FormItem to filter the default value.
# Arguments:
#   default value, as supplied by the user
#	C++ type
# Returns a new default value.
sub filter_default_value
{
	my ($self,$default_value,$cpp_type) = @_;
	if(exists $$self{'__default_value_filter'})
	{
		return &{$$self{'__default_value_filter'}}(@_);
	}
	return $default_value
}

# Set the default filter on this form item.
# Returns the old filter, if one was set.
sub set_default_value_filter
{
	my ($self,$filter) = @_;
	my $old = $$self{'__default_value_filter'};
	$$self{'__default_value_filter'} = $filter;
	$old
}

1;

