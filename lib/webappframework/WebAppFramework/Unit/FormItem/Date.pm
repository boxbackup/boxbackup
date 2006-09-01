package WebAppFramework::Unit::FormItem::Date;
use strict;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;
use WebAppFramework::DateAndTime;
use vars qw/%letter_to_index %letter_to_name/;

# new() parameters:
#	Name => Form item name
# 	Default => standard date specifier
#	Optional => set if the field is optional
#	StartYear => first year displayed
#	EndYear => last year displayed
#	ValidationFailMsg => text to display on error

# used for allocating IDs
%letter_to_index = ('Y' => 0, 'M' => 1, 'D' => 2);
# used for getting translated text
%letter_to_name = ('Y' => 'Year', 'M' => 'Month', 'D' => 'Day');

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# work out where the default values for the field come from
		my @defaults = ('','','');
		if(exists $$self{'Default'})
		{
			@defaults = WebAppFramework::DateAndTime::make_YMD_params_array(
				$self,
				$self->list_to_array($$self{'Default'})
			);
		}

		# get the locale
		my $locale = $self->get_locale();
		
		# get the order in which the fields are to be written from the locale
		my $order = $locale->get('FormItemDateOrdering');

		# IDs used
		my @element_ids = $self->get_ids();
		
		# What's the object in the form?
		my $current_value_name = $self->get_form()->get_form_name().'.Get'.$self->get_item_name().'()';
		
		# start off the table which contains the form
		$output->write_text('<table cellpadding=0 cellspacing=4 border=0><tr>');
		
		# write each item in turn
		for my $element_letter (split //,$order)
		{
			my $element_id = $element_ids[$letter_to_index{$element_letter}];
			my $unselected_text = '-- '.$locale->get($letter_to_name{$element_letter}).' --';
		
			if($element_letter eq 'Y')
			{
				# start the item
				$output->write_text(qq!<td><select name="$element_id"><option value="">$unselected_text</option>!);
				
				# write the for loop
				my $start = int($$self{'StartYear'});
				my $end = int($$self{'EndYear'});

				# if they're zero, then it's a page variable
				$start = $self->get_variable($$self{'StartYear'})->convert_to('int32_t') if $start == 0;
				$end = $self->get_variable($$self{'EndYear'})->convert_to('int32_t') if $end == 0;
				
				# Finally that for loop
				my $selected_val = ($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
						?($defaults[$letter_to_index{$element_letter}])
						:($current_value_name.'.GetYear()');
				my $selval = ($selected_val eq '')?'':"\nint selectedValue = ($selected_val);";
				$output->write_code(<<__E);
					{
						int yearStart = ($start);	// in case it's expensive to get these values
						int yearEnd = ($end);
						int inc = (yearStart < yearEnd)?+1:-1;$selval
						for(int year = yearStart; year != yearEnd; year += inc)
						{
							char output[128];
__E
				if($selected_val eq '')
				{
					$output->write_code(qq!rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%04d</option>", year, year));\n!);
				}
				else
				{
					$output->write_code(<<__E);
							if(year == selectedValue)
							{
								rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\" selected>%04d</option>", year, year));
							}
							else
							{
								rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%04d</option>", year, year));
							}
__E
				}

				$output->write_code(<<__E);
						}
					}			
__E

				# end the item
				$output->write_text(qq!</select></td>!);
			}
			elsif($element_letter eq 'M')
			{
				# start the item
				$output->write_text(qq!<td><select name="$element_id"><option value="">$unselected_text</option>!);
				
				# Write the code
				my $selected_val = ($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
						?($defaults[$letter_to_index{$element_letter}])
						:($current_value_name.'.GetMonth()');
				my $selval = ($selected_val eq '')?'':"\nint selectedValue = ($selected_val);";
				$output->write_code(<<__E);
					{
						// Get the month names from the locale
						const char **monthNames = locale.GetMonthList();$selval
						for(int month = 1; month <= 12; ++month)
						{
							char output[128];
__E
				if($selected_val eq '')
				{
					$output->write_code(qq!rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%s</option>", month, monthNames[month]));\n!);
				}
				else
				{
					$output->write_code(<<__E);
							if(month == selectedValue)
							{
								rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\" selected>%s</option>", month, monthNames[month]));
							}
							else
							{
								rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%s</option>", month, monthNames[month]));
							}
__E
				}
				
				$output->write_code(<<__E);
						}
					}			
__E
				# end the item
				$output->write_text(qq!</select></td>!);
			}
			elsif($element_letter eq 'D')
			{
				# start the item
				$output->write_text(qq!<td><select name="$element_id"><option value="">$unselected_text</option>!);

				# Finally write that for loop
				my $selected_val = ($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
						?($defaults[$letter_to_index{$element_letter}])
						:($current_value_name.'.GetDay()');
				if($selected_val eq '')
				{
					# initiall it's an unselected value, so just precompute the entire list of values
					for(my $day = 1; $day <= 31; $day++)
					{
						$output->write_text(qq!<option value="$day">$day</option>!);
					}
				}
				else
				{
					# a value needs to be selected
					$output->write_code(<<__E);
						{
							int selectedValue = ($selected_val);
							for(int day = 1; day <= 31; ++day)
							{
								char output[128];
__E
					if($selected_val eq '')
					{
						$output->write_code(qq!rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%d</option>", day, day));\n!);
					}
					else
					{
						$output->write_code(<<__E);
								if(day == selectedValue)
								{
									rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\" selected>%d</option>", day, day));
								}
								else
								{
									rResponse.Write(output, ::sprintf(output, "<option value=\\"%d\\">%d</option>", day, day));
								}
__E
					}
	
					$output->write_code(<<__E);
							}
						}			
__E
				}

				# end the item
				$output->write_text(qq!</select></td>!);
			}
			else
			{
				die "Unknown element letter $element_letter specified in Locale FormItemDateOrdering item"
			}
		}
	
		# close the table
		$output->write_text('</tr></table>');
	}
}

sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name, $element_id) = @_;
	
	# which field is this?
	my @element_ids = $self->get_ids();
	my @element_names = qw/Year Month Day/;
	my $element_name;
	for(my $l = 0; $l < 3; $l++)
	{
		$element_name = $element_names[$l] if $element_ids[$l] eq $element_id;
	}
	$output->write_code($validity_error_name .= ' = m'.$self->get_item_name().'.DataFromForm(WAFFormItemDate::'
			.$element_name.', '.$data_source->convert_to('std::string').'.c_str(), '
			.(($$self{'Optional'} ne '')?'true':'false').");\n");
}

sub write_validation_fail_message
{
	my ($self, $output) = @_;
	
	if(exists $$self{'ValidationFailMsg'})
	{
		$output->write_text_translated($$self{'ValidationFailMsg'})
	}
	else
	{
		$output->write_text_translated('Please enter a valid date')
	}
}

# need the data type header in the page definition
sub get_required_headers_this
{
	my ($self, $type) = @_;
	if($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)
	{
		return ('WAFFormItemDate.h')
	}
	elsif($type == WebAppFramework::Unit::HEADERS_SYSTEM)
	{
		return ('stdio.h')
	}
	return ()
}

# Three ids are required for this form item: year, month, day
sub get_num_ids_required
{
	return 3;
}

# use a special class for the data type
sub get_form_variable_type
{
	return 'WAFFormItemDate'
}

# if the form item is optional, then the default validation state is Valid
sub default_validation_state
{
	my ($self) = @_;
	return ($$self{'Optional'} ne '')?'WebAppForm::Valid':'WebAppForm::NotValid'
}


1;
