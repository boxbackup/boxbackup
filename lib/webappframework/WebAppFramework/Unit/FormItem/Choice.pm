package WebAppFramework::Unit::FormItem::Choice;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;
use WebAppFramework::Unit::DataSource::StaticStrings;

# new() parameters:
#	Name => Form item name
#	Choices => | separated list of choices (will be translated) -- if a sub-unit DataSource isn't added
# 	Default => CONSTANT: untranslated default choice (from list), or variable name. If not included, a blank item will be displayed
#	Style => select (HTML select widget) or items (checkboxes or radio buttons)
#	Size => rows visible (for select mode only)
#	Columns => number of columns (for items mode only)
#	Validation =>
#		none	- no checking (single choice widget)
#		single	- single choice must be made (single choice widget)
#		choices(x,y)	- numbers allowed within range x to y inclusive. Omit for unbounded range. (multiple choice widget)

# If a sub-unit at position DataSource exists, it will be used as a source of the options.
# These should be derived from WebAppFramework::Unit::DataSource.


# the maximum number of choices written as inline code, rather than a loop
use constant MAX_INLINE_CHOICES =>	8;

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# Type of C++ variable
		$self->ensure_cpptype_found();
		my $cpptype = $$self{'_cpptype'};
	
		# Are the choices in this control bound to another data source?
		my $binding = undef;
		my @choices_un;
		my @choices;
		if(exists ${$$self{'_units'}}{'DataSource'})
		{
			$binding = ${$$self{'_units'}}{'DataSource'};
		}
		else
		{
			# untranslated choices (for default checking)
			@choices_un = split /\|/,$$self{'Choices'};
			# translated items (for display)
			@choices = split /\|/,$output->translate_text($$self{'Choices'});
			
			# check!
			die "Choices form item ".$$self{'Name'}." has no choices set"
				if $#choices_un < 0;
			die "Choices form item ".$$self{'Name'}.", translation has different number of choices"
				if $#choices_un != $#choices;
		}
		
		# default?
		my $constant_default = -1;
		my $var_default = undef;
		if(exists $$self{'Default'})
		{
			if($$self{'Default'} =~ m/\ACONSTANT:(.+?)\Z/)
			{
				die "Cannot use a CONSTANT:default in Choices FormItem when a DataBinding is in use"
					unless $#choices_un >= 0;		# don't check on $binding, because it might be an automatic StaticStrings binding

				my $def = $1;
				for(my $i = 0; $i <= $#choices_un; $i++)
				{
					if($choices_un[$i] eq $def)
					{
						$constant_default = $i;
						last
					}
				}
				die "Choices form item ".$$self{'Name'}.", default $def cannot be found in choices list"
					if $constant_default == -1;
			}
			else
			{
				$var_default = $self->get_variable($$self{'Default'});
			}
		}
		# Filter the default
		{
			my $v = (defined $var_default)
						?$var_default
						:cppvar($cpptype, ($cpptype eq 'std::string')?qq!"$constant_default"!:$constant_default);
			my $v_f = $self->filter_default_value($v, $cpptype);
			if($v_f != $v)
			{
				# the filter changed something, use the new value
				$constant_default = -1;
				$var_default = $v_f;
			}
		}
		
		# useful stuff...
		my $select_style = $self->_is_select_style();
		my $attr = $self->get_tag_attributes();
		my $id = $self->get_id();
		my $choose_multiple = $self->_can_choose_multiple();
		my $input_type = ($choose_multiple)?'checkbox':'radio';
		my $size = (exists $$self{'Size'})?(int($$self{'Size'})):1;
		$size = 1 if $size <= 0;
		# make sure that multiple choice things have at least two items to play with
		$size = 2 if $choose_multiple && $size < 2;
		# columns?
		my $columns = 0;
		if(!$select_style)
		{
			$columns = int($$self{'Columns'}) if exists $$self{'Columns'}
		}

		# start boilerplate?
		if($select_style)
		{
			my $s = ($size == 1)?'':qq` size="$size"`;
			my $m = ($choose_multiple)?' multiple':'';
			$output->write_text(qq`<select name="$id"$s$m$attr>`);
			if(!(exists $$self{'Default'}) && $size == 1)
			{
				# no default... write blank entry
				$output->write_text(qq`<option value="">`.$output->translate_text('-- select --').'</option>')
			}
		}
		elsif($columns > 0)
		{
			# Is multiple columns item style form item -- output table header
			$output->write_text('<table cellpadding=0 cellspacing=2 border=0><tr><td>')
		}
		
		my $constant_output = (!defined($var_default) && $subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT);
		
		if(!$constant_output)
		{
			# non-constant field default, or in second phase...
			my $value_source = ($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_DEFAULT)
				?$var_default:$self->get_variable($self->get_value_page_variable());
			if($choose_multiple && $subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_REDISPLAY)
			{
				$output->write_code("{\nconst std::vector<${cpptype}> &v(".$value_source->convert_to("std::vector<${cpptype}>").");\n");
			}
			else
			{
				$output->write_code("{\n${cpptype} v = ".$value_source->convert_to($cpptype).";\n");
			}
		}
		
		# Binding or static choices?
		if(defined $binding)
		{
			if(!$select_style && $columns > 0)
			{
				# need to keep a separate count of the cells output
				$output->write_code("int32_t cellsWritten = 0;\n");
			}

			# Generate a function to write the code
			my $function;
			my $strings_trusted = $binding->string_is_trusted();
			$function = sub
				{
					my ($key_source,$string_source,$output,$phase,$subphase) = @_;

					# if condition for selection
					my $if_cond;
					if($subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_REDISPLAY)
					{
						# form is being redisplayed
						$if_cond = ($choose_multiple)
							?"std::find(v.begin(), v.end(), $key_source) != v.end()":"v == $key_source";
					}
					else
					{
						# initial form display -- only one choice
						$if_cond = ($constant_output)
							?("$key_source == $constant_default"):("v == $key_source")
					}
		
					$output->write_text($select_style?'<option value="':qq`<input type="$input_type" name="$id" value="`);
					if($cpptype eq 'int32_t')
					{
						$output->write_code(<<__E);
							{
								char str[32];
								rResponse.Write(str, ::sprintf(str, "%d", $key_source));
							}
__E
					}
					else
					{
						# The keys can't necessarily be trusted either
						if($strings_trusted)
						{
							$output->write_code("rResponse.WriteString($key_source);\n");
						}
						else
						{
							$output->write_code("rResponse.WriteStringDefang($key_source);\n");
						}
					}
					# finish off the option, with the selection thing
					if(!$constant_output || $constant_default != -1)
					{
						$output->write_code("if($if_cond)\n{\n");
						$output->write_text($select_style?'" selected>':'" checked>');
						$output->write_code("}\nelse\n{\n");
					}
					$output->write_text('">');
					if(!$constant_output || $constant_default != -1)
					{
						$output->write_code("}\n");
					}
					# write the bound data
					if($strings_trusted)
					{
						$output->write_code("rResponse.WriteString($string_source);\n");
					}
					else
					{
						$output->write_code("rResponse.WriteStringDefang($string_source);\n");
					}
					$output->write_text($select_style?'</option>':'</input>');
					if(!$select_style)
					{
						# got to write a separator here...
						if($columns > 0)
						{
							# going to need some code here...
							my $tv = $columns-1;
							$output->write_code("if((cellsWritten % $columns) == $tv)\n{\n");
							$output->write_text('</td></tr><tr><td>');
							$output->write_code("}\nelse\n{\n");
							$output->write_text('</td><td>');
							$output->write_code("}\n++cellsWritten;\n");
						}
						else
						{
							# no columns, just use <br>
							$output->write_text('<br>');
						}
					}
				};
		
			# Ask the binding to write the necessary code
			$binding->write_bound_item($function, $output, $phase, $subphase);
		}
		else
		{
			# Do each item in turn
			for(my $i = 0; $i <= $#choices; $i++)
			{
				my $choosen_test = ($choose_multiple && $subphase == WebAppFramework::Unit::Form::OUTPUT_SUB_PHASE_REDISPLAY)
							?"std::find(v.begin(), v.end(), $i) != v.end()":"v == $i";
						
				if($select_style)
				{
					$output->write_text(qq`<option value="$i"`);
					if($constant_output)
					{
						$output->write_text(($i == $constant_default)?' selected':'');
					}
					else
					{
						$output->write_code("if($choosen_test)\n{\n");
						$output->write_text(' selected');
						$output->write_code("}\n");				
					}
					$output->write_text('>'.$choices[$i].'</option>');
				}
				else
				{
					$output->write_text(qq`<input type="$input_type" name="$id" value="$i"`);
					if($constant_output)
					{
						$output->write_text(($i == $constant_default)?' checked':'');
					}
					else
					{
						$output->write_code("if($choosen_test)\n{\n");
						$output->write_text(' checked');
						$output->write_code("}\n");				
					}
					$output->write_text($attr.'>'.$choices[$i].'</input>');
					if($columns > 0)
					{
						# columns, write table rows/column separator
						if($i != $#choices && (($i % $columns) == ($columns-1)))
						{
							# new row
							$output->write_text('</td></tr><tr><td>')
						}
						else
						{
							# new cell
							$output->write_text('</td><td>')
						}
					}
					else
					{
						# no columns, just use <br>
						$output->write_text('<br>') if $i != $#choices;
					}
				}
			}
		}
		
		$output->write_code("}\n")
			unless $constant_output;

		# end boilerplate
		if($select_style)
		{
			$output->write_text("</select>")
		}
		elsif($columns > 0)
		{
			# Is multiple columns item style form item -- output table footer
			$output->write_text('</td></tr></table>')
		}
	}
	
	# use the default Unit write_unit to make sure things like data sources
	# have a chance to write all their other stuff required
	WebAppFramework::Unit::write_unit($self, $output, $phase, $subphase);
}

# get variables, make sure they have the right default
sub get_form_variables
{
	my ($self) = @_;
	$self->ensure_cpptype_found();
	my $cpptype = $$self{'_cpptype'};
	if($self->_can_choose_multiple())
	{
		return (cppvar("std::vector<${cpptype}>", $$self{'Name'}))
	}
	else
	{
		return (cppvar($cpptype, $$self{'Name'}, ($cpptype eq 'std::string')?undef:'WebAppForm::NoChoiceMade'))
	}
}

# if validation is 'none', then it'll always pass validation
sub always_passes_validation
{
	my ($self) = @_;
	my $validation = $self->get_item_validation();
	return ($validation eq 'none' || $validation =~ m/\Achoices\(\s*,\s*\)\Z/)
}


sub write_value_acceptance_code
{
	my ($self, $output, $data_source, $validity_error_name) = @_;
	
	$self->ensure_cpptype_found();
	my $cpptype = $$self{'_cpptype'};

	# validation method
	my $validation = $self->get_item_validation();
	
	# data value within the form
	my $dv = 'm'.$self->get_item_name();
		
	# decode the value given
	my $in = $data_source->convert_to('std::string');
	if($cpptype eq 'int32_t')
	{
		# integer type
		my ($min, $max);
		if(exists ${$$self{'_units'}}{'DataSource'})
		{
			my $binding = ${$$self{'_units'}}{'DataSource'};
			($min,$max) = $binding->get_integer_range();
		}
		else
		{
			$min = 0;
			# calc max choice number from the untranslated strings
			my @choices_un = split /\|/,$$self{'Choices'};
			$max = $#choices_un + 1;
		}
		my $min_c = ($min eq '')?'':" || v < $min";
		my $max_c = ($max eq '')?'':" || v > $max";
		
		$output->write_code(<<__E);
			errno = 0;	// necessary on some platforms
			int32_t v = ::strtol($in.c_str(), NULL, 10);
			if(!(($in).empty() || (v == 0 && errno == EINVAL) || v == LONG_MIN || v == LONG_MAX$min_c$max_c))
			{
__E
	}
	else
	{
		# string type
		$output->write_code("std::string v($in);\n{\n");
	}
	
	if($self->_can_choose_multiple())
	{
		# multiple choices
		$output->write_code("$dv.push_back(v);\n}\n");
		
		# validation
		die "Bad validation $validation for Choices FormItem"
			unless $validation =~ m/\Achoices\((\d*)\s*,\s*(\d*)\)\Z/;
		my ($min,$max) = ($1,$2);
		if($min ne '' && $max eq '')
		{
			$output->write_code("$validity_error_name = ($dv.size() >= $min)?WebAppForm::Valid:WebAppForm::NotValid;\n")
		}
		elsif($min eq '' && $max ne '')
		{
			$output->write_code("$validity_error_name = ($dv.size() <= $max)?WebAppForm::Valid:WebAppForm::NotValid;\n")
		}
		elsif($min ne '' && $max ne '')
		{
			$output->write_code("$validity_error_name = ($dv.size() >= $min && $dv.size() <= $max)?WebAppForm::Valid:WebAppForm::NotValid;\n")
		}
	}
	else
	{
		# single choice only
		$output->write_code("$dv = v;\n}\n");

		# validate
		if($validation ne 'none')
		{
			my $cond = ($cpptype eq 'int32_t')?"$dv != WebAppForm::NoChoiceMade":"!($dv.empty())";
			$output->write_code("$validity_error_name = ($cond)?WebAppForm::Valid:WebAppForm::NotValid;\n")
		}
	}
}

sub make_validation_fail_message
{
	my ($self) = @_;
	
	my $inline_errs = $self->get_form()->inline_error_messages();
	my $label = $self->get_item_label();

	my $t;

	if($self->_can_choose_multiple())
	{
		my $validation = $self->get_item_validation();
		
		$validation =~ m/\Achoices\((\d*)\s*,\s*(\d*)\)\Z/;
		my ($min,$max) = ($1,$2);
		if($min ne '' && $max eq '')
		{
			$t = "Please choose at least $min options"
		}
		elsif($min eq '' && $max ne '')
		{
			$t = "Please choose no more than $max options"
		}
		elsif($min ne '' && $max ne '')
		{
			$t = "Please choose between $min and $max options"
		}
	}
	else
	{
		$t = 'Please choose an option'
	}
	
	if(!$inline_errs)
	{
		$t .= ' for '.$label
	}
	
	$t
}

# will need some extra headers
sub get_required_headers_this
{
	my ($self, $type) = @_;
	
	my @h;

	# Standard headers required
	if($type == WebAppFramework::Unit::HEADERS_PAGE_H_SYSTEM)
	{
		push @h,'stdlib.h','limits.h','errno.h';

		if($self->_can_choose_multiple())
		{
			push @h,'vector','algorithm';
		}
	}

	# for integer conversions (used with DataSources, too complex to make it worth including
	# only if there is a DataSource subunit because of when we know we've got such a unit.)
	if($type == WebAppFramework::Unit::HEADERS_SYSTEM)
	{
		push @h,'stdio.h';
	}

	@h
}


sub ensure_cpptype_found
{
	my ($self) = @_;
	return if exists $$self{'_cpptype'};

	# See what C++ type we'll be using, count the number of choices
	my $cpptype = 'int32_t';
	if(exists ${$$self{'_units'}}{'DataSource'})
	{
		my $binding = ${$$self{'_units'}}{'DataSource'};
		$cpptype = 'std::string' unless $binding->key_is_integer();
	}
	else
	{
		# automatically use a StaticStrings DataSource for large amounts of choices?
		my $l = $$self{'Choices'};
		$l =~ tr/|//cd;
		if(length($l) >= MAX_INLINE_CHOICES)
		{
			$self->add_unit('DataSource',
				WebAppFramework::Unit::DataSource::StaticStrings->new('Strings' => $$self{'Choices'}));
		}
	}

	# and store
	$$self{'_cpptype'} = $cpptype;
}

sub _is_select_style
{
	my ($self) = @_;
	
	if($$self{'Style'} eq 'select')
	{
		return 1;
	}
	elsif($$self{'Style'} eq 'items')
	{
		return 0
	}
	else
	{
		die "Choices form item ".$$self{'Name'}.", style is not select or items"
	}
}

sub _can_choose_multiple
{
	my ($self) = @_;

	my $validation = $self->get_item_validation();

	if($validation eq 'single' || $validation eq 'none')
	{
		# not multiple choice
		return 0;
	}
	else
	{
		# multiple things can be selected
		return 1;
	}
}

1;

