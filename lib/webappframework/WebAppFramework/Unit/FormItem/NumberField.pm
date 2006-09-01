package WebAppFramework::Unit::FormItem::NumberField;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;
use WebAppFramework::FixedPoint;

# new() parameters:
#	Name => Form item name
# 	Default => source of default value (can be CONSTANT:value for constant values)
#	DefaultNumber => a default number to use when conversion fails completely
#	FixedPointScaleDigits => number of digits for (base 10) fixed point values
#	FixedPointDisplayDigits => min number of digits to use for the fractional part. Defaults to FixedPointScaleDigits.
#			note: When fixed point in use, specify all numbers in human readable form, eg 1.01
#	BlankValue => a special value to use when the field is blank (blank not allowed if this value is not present)
#	Size => size of field
#	Validation =>
#		none	- no range checking
#		range(x,y)	- numbers allowed within range x to y inclusive. Omit for unbounded range.


sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# Generate attributes
		my $attr = '" name="'.$self->get_id().'"'.$self->get_tag_attributes();
		my $size = int($$self{'Size'});
		$size = 12 if $size <= 0;
		
		# Work out maxlength attribute
		my $max_len = -1;
		my $validation = $self->get_item_validation();
		if($validation =~ m/\Arange\((-?\d*)\s*,\s*(-?\d*)\)\Z/)
		{
			# use values to determine the size of the field
			if($1 ne '' && $2 ne '')
			{
				$max_len = length($1);
				$max_len = length($2) if length($2) > $max_len;
				
				# take into account the fixed-pointness
				if(exists $$self{'FixedPointScaleDigits'})
				{
					# add on the number of digits, plus room for the '.'
					$max_len += int($$self{'FixedPointScaleDigits'}) + 1;
				}
			}
		}
		# make sure it has a sensible default, which is 11, the size of the largest integer that can be expected
		$max_len = 11 if $max_len <= 0;
	
		$attr .= qq` size="$size" maxlength="$max_len"`;

		# write the item
		if($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
		{
			$output->write_text('<input type="text"'.$attr);

			# write a default value?
			my $def;
			my $blank_val = (exists $$self{'BlankValue'})?($$self{'BlankValue'}):'0x7fffffff';
			if(exists $$self{'Default'})
			{
				if($$self{'Default'} =~ m/\ACONSTANT:(.*)\Z/)
				{
					$def = cppvar('int32_t', $self->specified_value_to_int($1))
				}
				else
				{
					$def = $self->get_variable($$self{'Default'})
				}
			}
			else
			{
				$def = cppvar('int32_t', $blank_val);
			}
			my $def_f = $self->filter_default_value($def, 'int32_t');
			if($def_f == $def)
			{
				# no filtering (if it's a constant, it'll end up as static text)
				if(exists $$self{'Default'})
				{
					$output->write_text(' value="');
					# special behaviour for fixed point
					if(exists $$self{'FixedPointScaleDigits'})
					{
						# constant value?
						if($$self{'Default'} =~ m/\ACONSTANT:(.*)\Z/)
						{
							# just write whatever the user specified
							$output->write_text($1);
						}
						else
						{
							# write it as a fixed point value
							WebAppFramework::FixedPoint::write_fixed_point_value($output,
								$self->get_variable($def), $$self{'FixedPointScaleDigits'}, $$self{'FixedPointDisplayDigits'});
						}
					}
					else
					{
						$self->write_variable_text($output, $$self{'Default'});
					}
					$output->write_text('"');
				}
			}
			else
			{
				$output->write_text(' value="');
				my $d = $def_f->convert_to('int32_t');
				$output->write_code(<<__E);
					{
						int32_t v = $d;
						if(v != $blank_val)
						{
__E
				if(exists $$self{'FixedPointScaleDigits'})
				{
					# write it as a fixed point value
					WebAppFramework::FixedPoint::write_fixed_point_value($output,
						$self->get_variable('=int32_t v'), $$self{'FixedPointScaleDigits'}, $$self{'FixedPointDisplayDigits'});
				}
				else
				{
					$self->write_variable_text($output, '=int32_t v');
				}
				$output->write_code("}\n}\n");
				$output->write_text('"');			
			}

			$output->write_text('>');
		}
		else
		{
			my $bv_check = '';
			if(exists $$self{'BlankValue'})
			{
				$bv_check = $self->get_variable($self->get_value_page_variable())->convert_to('int32_t')." != ".$self->specified_value_to_int($$self{'BlankValue'})." && "
			}		
			$output->write_text('<input type="text"'.$attr.' value="');
			my $v = $self->get_form()->get_item_validity_error($self);
			$output->write_code("if(${bv_check}$v != WebAppForm::NumberFieldErr_Blank && $v != WebAppForm::NumberFieldErr_FormatBlank)\n{\n");
			if(exists $$self{'FixedPointScaleDigits'})
			{
				# write it as a fixed point value
				WebAppFramework::FixedPoint::write_fixed_point_value($output,
					$self->get_variable($self->get_value_page_variable()), $$self{'FixedPointScaleDigits'}, $$self{'FixedPointDisplayDigits'});
			}
			else
			{
				$self->write_variable_text($output, $self->get_value_page_variable());
			}
			$output->write_code("}\n");
			$output->write_text('">');
		}
	}
}

sub specified_value_to_int
{
	my ($self,$v) = @_;
	return int($v) unless exists $$self{'FixedPointScaleDigits'};
	
	# how many digits?
	my $digits = int($$self{'FixedPointScaleDigits'});
	
	# adjust the value
	my $mul = 1;
	for(my $l = 0; $l < $digits; $l++) {$mul *= 10;}
	return int($v * $mul);
}

sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name) = @_;
	
	# validation method
	my $validation = $self->get_item_validation();
	
	# data value within the form
	my $dv = 'm'.$self->get_item_name();
	
	my ($range_begin,$range_end) = ('','');

	# check the validation string
	if($validation eq 'none')
	{
		# leave both ends of the range blank
	}
	elsif($validation =~ m/\Arange\((-?[\d\.]*)\s*,\s*(-?[\d\.]*)\)\Z/)
	{
		($range_begin,$range_end) = ($1,$2)
	}
	else
	{
		# bad value!
		die "Bad validation for NumberField: ".$validation
	}
	
	# data value within the form
	my $dv = 'm'.$self->get_item_name();
	
	my $blank_val = '0';
	my $blank_allowed = 'false';
	if(exists $$self{'BlankValue'})
	{
		$blank_val = $$self{'BlankValue'};
		$blank_allowed = 'true';
	}
	
	# handle the fixed point case
	my $fnname_extra = '';
	my $fnargs_extra = '';
	if(exists $$self{'FixedPointScaleDigits'})
	{
		$fnname_extra = 'FixedPoint';
		$fnargs_extra = ', '.int($$self{'FixedPointScaleDigits'});
	}
	
	# store the data value
	$output->write_code($validity_error_name ." = WAFUtility::IntepretNumberField${fnname_extra}("
		.$data_source->convert_to('std::string').
		", $dv, ".$self->specified_value_to_int($blank_val).", $blank_allowed, "
		.$self->specified_value_to_int($range_begin).", "
		.$self->specified_value_to_int($range_end).", ".
		(($range_begin eq '')?'false':'true').", ".(($range_end eq '')?'false':'true')."$fnargs_extra);\n");
}

# get variables, make sure they have the right default
sub get_form_variables
{
	my ($self) = @_;
	die "NumberField doesn't have DefaultNumber specified"
		unless exists $$self{'DefaultNumber'};
	return (cppvar('int32_t', $$self{'Name'}, $$self{'DefaultNumber'}))
}

# the possible values of the validition fail error
sub get_values_of_validation_fail_messages
{
	return qw/WebAppForm::NumberFieldErr_Range WebAppForm::NumberFieldErr_Format|WebAppForm::NumberFieldErr_FormatBlank WebAppForm::NumberFieldErr_Blank/;
}

sub make_validation_fail_message
{
	my ($self, $err_val) = @_;

	my $inline_errs = $self->get_form()->inline_error_messages();
	my $label = $self->get_item_label();
	
	if($err_val eq 'WebAppForm::NumberFieldErr_Range')
	{
		my $validation = $self->get_item_validation();
		if($validation =~ m/\Arange\((-?[\d\.]*)\s*,\s*(-?[\d\.]*)\)\Z/)
		{
			return $label . " must be a number between $1 and $2 inclusive"
				if $1 ne '' && $2 ne '';
			return $label . " must be a number greater or equal to $1"
				if $1 ne '';
			return $label . " must be a number less than or equal to $2"
				if $2 ne '';
		}

		return $label . " must be a valid number"
	}
	elsif($err_val eq 'WebAppForm::NumberFieldErr_Format')
	{
		return $inline_errs?'You entered some non-digit characters, please check'
			:"You entered some non-digit characters in $label, please check";
	}
	elsif($err_val eq 'WebAppForm::NumberFieldErr_Blank')
	{
		return $inline_errs?'Please enter a number':$label . ' has been left blank';
	}

	'unknown error type'
}


1;

