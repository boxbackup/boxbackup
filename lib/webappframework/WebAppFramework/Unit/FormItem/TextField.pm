package WebAppFramework::Unit::FormItem::TextField;
use strict;
use base 'WebAppFramework::Unit::FormItem';
use WebAppFramework::Validator;
use CppVariable;

# new() parameters:
#	Name => Form item name
# 	Default => source of default value (can be CONSTANT:value for constant values)
#	DisplayAsPassword => if present, then display the field as a password entry
#	Size => size of field
#	MaxLength => max size of entered text, if browser respects this
#	BlankAllowed => if present, then a blank field is an acceptable value which passes validation

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# field size
		my ($size,$size_y) = split /\s*,\s*/,$$self{'Size'};
		$size = int($size);
		$size_y = int($size_y);

		# Generate attributes
		my $attr = ' type="'.$self->_ftype().'" name="'.$self->get_id().'"';
		$attr .= $self->get_tag_attributes();
		$attr .= qq` size="$size"` if $size > 0;
		my $maxlength = int($$self{'MaxLength'});
		$attr .= qq` maxlength="$maxlength"` if $maxlength > 0;

		# should we use a text area?
		my $is_text_area = ($size != 0 && $size_y != 0);
		if($is_text_area)
		{
			# redo the attributes
			$attr = ' name="'.$self->get_id().'"';
			$attr .= $self->get_tag_attributes();
		}
	
		# write the item
		if($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
		{
			# Filter the default value (if any)
			my $default = $self->filter_default_value($$self{'Default'}, 'std::string');
		
			# text area or normal?
			if($is_text_area)
			{
				$output->write_text(qq!<textarea$attr cols="$size" rows="$size_y">!);
				if(defined $default)
				{
					$self->write_variable_text($output, $default);
				}
				$output->write_text(qq!</textarea>!);
			}
			else
			{
				# normal field
				$output->write_text("<input$attr");
				# write a default value? (if it's a constant, it'll end up as static text)
				if(defined $default)
				{
					$output->write_text(' value="');
					$self->write_variable_text($output, $default);
					$output->write_text('"');
				}
				$output->write_text('>');
			}
		}
		else
		{
			if($is_text_area)
			{
				$output->write_text(qq!<textarea$attr cols="$size" rows="$size_y">!);
				$self->write_variable_text($output, $self->get_value_page_variable());
				$output->write_text(qq!</textarea>!);
			}
			else
			{
				# normal field
				$output->write_text(qq`<input$attr value="`);
				$self->write_variable_text($output, $self->get_value_page_variable());
				$output->write_text('">');
			}
		}
	}
}

# Field is always a string
sub get_form_variable_type
{
	my ($self) = @_;
	return ('std::string')
}

sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name) = @_;
	
	# make sure validator is created (if applicable)
	my $validator = $self->ensure_validator();

	# validation method
	my $validation = $self->get_item_validation();
	
	# data value within the form
	my $dv = 'm'.$self->get_item_name();
	
	# store the data value
	$output->write_code($dv .' = '.$data_source->convert_to('std::string'). ";\n");

	if(exists $$self{'BlankAllowed'})
	{
		# make blank values allowed
		$output->write_code("if($dv.empty())\n{\n$validity_error_name = WebAppForm::Valid;\n}\nelse\n{\n")
	}

	# do validation
	if(defined $validator)
	{
		# use the supplied validator
		$output->write_code("bool validatorResult = false;\n");
		$output->write_code($validator->generate_code(cppvar('std::string', $dv), 'validatorResult'));
		$output->write_code("$validity_error_name = validatorResult?(WebAppForm::Valid):(WebAppForm::NotValid);\n");
	}
	elsif($validation eq 'none')
	{
		# no validation, always is OK -- and there won't be any *Valid variable created, so do nothing
	}
	elsif($validation eq 'external')
	{
		# external validation -- always false
		$output->write_code($validity_error_name . " = WebAppForm::NotValid;\n")
	}
	elsif($validation =~ m/\Alength\((\d*),(\d*)\)\Z/)
	{
		my ($min,$max) = ($1,$2);
		die "TextField length(min,max) specified, but both range ends are unspecified" if $min eq '' && $max eq '';
			
		$output->write_code($validity_error_name . ' = ('. join(' && ',
			($min ne '')?('('.$dv.".size() >= $min)"):(),
			($max ne '')?('('.$dv.".size() <= $max)"):(),
			) . ")?(WebAppForm::Valid):(WebAppForm::NotValid);\n")
	}
	else
	{
		die "Unknown TextField validation method $validation"
	}

	if(exists $$self{'BlankAllowed'})
	{
		# end the if clause to make blank values allowed
		$output->write_code("}\n")
	}
}

# type of field -- allows selection of password display
sub _ftype
{
	my ($self) = @_;
	return (exists $$self{'DisplayAsPassword'})?"password":"text"
}


# will need some extra headers if doing email validation
sub get_required_headers_this
{
	my ($self, $type) = @_;
	
	my @h;

	# make sure validator is created (if applicable)
	my $validator = $self->ensure_validator();

	if(defined $validator)
	{
		# Standard headers required
		if($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)
		{
			push @h,$validator->get_required_headers_this(0) # false, project headers
		}
		elsif($type == WebAppFramework::Unit::HEADERS_PAGE_H_SYSTEM)
		{
			push @h,$validator->get_required_headers_this(1) # true, system headers
		}
	}

	@h
}



# make a default error message
sub make_validation_fail_message
{
	my ($self) = @_;
	
	# make sure validator is created (if applicable)
	my $validator = $self->ensure_validator();

	my $validation = $self->get_item_validation();

	if(defined $validator)
	{
		return $validator->generate_default_error_message( $self->get_item_label())
	}
	elsif($validation eq 'none' || $validation eq 'external')
	{
		# no error message for these cases -- provided externally
		return '';
	}
	elsif($validation =~ m/\Alength\((\d*),(\d*)\)\Z/)
	{
		my ($min,$max) = ($1,$2);

		my $m = $self->get_item_label() . ' must be ';
		if($min ne '' && $max ne '')
		{
			$m .= "between $min and $max"
		}
		elsif($min ne '')
		{
			$m .= "at least $min"
		}
		else
		{
			$m .= "no more than $max"
		}
		$m .= ' characters long';
		
		return $m
	}

	die "Unknown TextField validation method $validation"
}

# some methods don't do validation
sub always_passes_validation
{
	my ($self) = @_;
	
	my $validation = $self->get_item_validation();

	return ($validation eq 'none')
}


# make the sure the validator object (if appropraite) is loaded
sub ensure_validator
{
	my ($self) = @_;

	unless(exists $$self{'_validator'})
	{
		# always make it _something_
		$$self{'_validator'} = undef;

		# get validation using base class		
		my $validation = $self->get_item_validation();
		
		# is it a reference?
		if(ref($validation))
		{
			# user supplied validator
			$$self{'_validator'} = $validation;
		}
		else
		{
			# is the validation specified a builtin type?
			$validation =~ /\A([^\(]*)\(?/;
			my $vn = $1;
			if($vn ne 'none' && $vn ne 'external' && $vn ne 'length')
			{
				# it's a validator... load it (will terminate the process if it's not found)
				$$self{'_validator'} = WebAppFramework::Validator::load_builtin($validation)
			}
		}
		# otherwise, remains as undef
	}
	
	return $$self{'_validator'}
}

1;
