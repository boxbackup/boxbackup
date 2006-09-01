package WebAppFramework::Unit::Form;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FragmentsTemplate';
use vars qw/%_allocated_ids/;
use CppDataClass;
use CppVariable;
use WebAppFramework::Unit::OutputIf;

# define constants before using field modules
use constant OUTPUT_SUB_PHASE_DEFAULT => 0;
use constant OUTPUT_SUB_PHASE_REDISPLAY => 1;

# can use phase values in range 80 -- 99


use WebAppFramework;
use WebAppFramework::Output;
use WebAppFramework::Unit::FormTableContainer;


# new() parameters:
#	FormName => name of form
#	FormID => one letter ID (only necessary if target is not this page)
#	ExternalErrors => errors are listed externally to the form
#	HandleSubmission => code which handles the submission of the form, or ref to function which writes it (optional)
#	FormValidation => none, simple, errorgen -- style of extra validation
#	ArgsToValidate => which objects should be passed to the Validate() function
#			Space separated list (or anon array) of Application, Request, Response.
#	PostSetAndValidateCode => optional, code which is output just after the form object is read and validated.
#	ErrorDisplayCondition => C++ condition for displaying the error display box (the one by default above the form)
#			If 'false', the code is not output.

# Add sub units, which contain FormItems, or use make_container to return a new
# FormTableContainer.
# Sub units are output in alphabetical order of position.

sub get_form_name
{
	my ($self) = @_;
	return $$self{'FormName'}
}

sub make_container
{
	my ($self, @a) = @_;
	
	my $i = 0;
	while(exists ${$$self{'_units'}}{'CONTAINER'.$i})
	{
		$i++
	}
	my $container = WebAppFramework::Unit::FormTableContainer->new(@a);
	$self->add_unit('CONTAINER'.$i, $container);
	$container
}

# fragments required
sub get_required_fragments
{
	my ($self) = @_;
	return qw/ErrorMarker/;
}

sub get_required_headers_this
{
	my ($self, $type) = @_;

	return ($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)?('WebAppForm.h'):();
}

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	$self->ensure_template_loaded($output, $phase, $subphase);

	# has a form ID been allocated?
	unless(exists $$self{'_form_id'})
	{
		if(exists $$self{'FormID'})
		{
			# author specified a form ID in initialisation
			my $i = $$self{'FormID'};
			
			die "Form ID '$i' is not exactly 1 character in length"
				unless length($i) == 1;
			
			die "Form ID already allocated for form name ".$$self{'FormName'}
				if exists $$self{'_form_id'};
			
			die "Form ID '$i' has already been used on this page"
				if exists $_allocated_ids{$i};
			
			$$self{'_form_id'} = $i;
			$_allocated_ids{$i} = $self;
		}
		else
		{
			# no form ID specified, generate one automatically
			for('a' .. 'z', 'A' .. 'Z')
			{
				unless(exists $_allocated_ids{$_})
				{
					$$self{'_form_id'} = $_;
					$_allocated_ids{$_} = $self;
					last;
				}
			}
			die "Could not automatically allocate ID for form ".$$self{'FormName'}." -- too many forms on the page?"
				unless exists $$self{'_form_id'};
		}
	}
	my $form_id = $$self{'_form_id'};
	
	# Parse the user validation setting
	my $has_user_validation = 0;
	my $user_validation_is_error_generating = 0;
	if(exists $$self{'FormValidation'})
	{
		if($$self{'FormValidation'} eq 'none')
		{
		}
		elsif($$self{'FormValidation'} eq 'simple')
		{
			$has_user_validation = 1;
		}
		elsif($$self{'FormValidation'} eq 'errorgen')
		{
			$has_user_validation = 1;
			$user_validation_is_error_generating = 1;
		}
		else
		{
			die "Unknown FormValidation style '".$$self{'FormValidation'}."'"
		}
	}
	
	# The following initialisation cannot be done in the initialise phase
	# because not all page variables will exist.
	if($phase != WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# parse any extra arguments required by the validation function
		unless(exists $$self{'_args_setandvalidate'})
		{
			my $args_setandvalidate = '';
			my $args_callsetandvalidate;
			my @args_validate;
			my @args_callvalidate;
			
			if(exists $$self{'ArgsToValidate'})
			{
				my $webappname = $self->get_webapp()->get_webapp_name();
			
				# process each entry in turn
				for my $a ($self->list_to_array($$self{'ArgsToValidate'}))
				{
					# one of the known items?
					if($a eq 'Application')
					{
						$args_setandvalidate .= ', '.$webappname.' &rApplication';
						$args_callsetandvalidate .= ', mApplication';
						push @args_validate,$webappname.' &rApplication';
						push @args_callvalidate,'rApplication';
					}
					elsif($a eq 'Response')
					{
						$args_setandvalidate .= ', HTTPResponse &rResponse';
						$args_callsetandvalidate .= ', rResponse';
						push @args_validate,'HTTPResponse &rResponse';
						push @args_callvalidate,'rResponse';
					}
					elsif($a eq 'Request')
					{
						# no set and validate arg, it's already passed
						push @args_validate,'const HTTPRequest &rRequest';
						push @args_callvalidate,'rRequest';
					}
					else
					{
						# it's a page variable
						my $var = $self->get_variable($a);
						
						# generate a name for it by replacing all non-alphanum chars with _
						my $nm = (($a =~ m/\A=/)?($var->name()):($a));
						$nm =~ tr/0-9a-zA-Z/_/c;
						$nm =~ s/_+\Z//;
						
						# type for arguments (pass as reference if a composite type)
						my $ty = $var->type();
						$ty = "const $ty&" if $var->is_composite_type();
						
						# build the various arguments and declarations...
						$args_setandvalidate .= ", $ty $nm";
						$args_callsetandvalidate .= ', '.$var->name();
						push @args_validate,"$ty $nm";
						push @args_callvalidate,$nm;
					}
				}
			}
			
			# store for later
			$$self{'_args_setandvalidate'} = $args_setandvalidate;
			$$self{'_args_callsetandvalidate'} = $args_callsetandvalidate;
			$$self{'_args_callvalidate'} = join(', ',@args_callvalidate);
			
			# add a functions to the data class?
			die "internal error, initialisation bad order" unless exists $$self{'_data_class'};
			if($has_user_validation)
			{
				# declare the validation function
				$$self{'_data_class'}->add_declarations(CppDataClass::PROTECTED, 'void Validate('.join(', ',@args_validate).');');
			}
			$$self{'_data_class'}->add_declarations(CppDataClass::PUBLIC, "void SetAndValidate(const HTTPRequest &rRequest$args_setandvalidate);");
		}
	}
	
	# Initialise everything
	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# get a list of all form items, in random order
		my @items = map {($_->isa('WebAppFramework::Unit::FormItem'))?$_:()} $self->flatten_heirarchy();
		
		# allocate IDs and collect form object variables in alphabetical order
		my %i;
		for(@items)
		{
			$i{$_->get_item_name()} = $_
		}
		my $idn = 0;
		my @object_vars;
		my @valid_vars;
		my @valid_fns;
		for(sort keys %i)
		{
			my $item = $i{$_};
			
			my $form_ids_required = $item->get_num_ids_required();
			my @ids;
			for(my $z = 0; $z < $form_ids_required; $z++)
			{
				push @ids,$form_id.sprintf("%03x", $idn++);
			}
			$item->set_ids(@ids);
			
			# collect variables
			my @fv = $item->get_form_variables();
			if($#fv >= 0)
			{
				# object has variables... add
				push @object_vars, @fv;
				# and a single 'valid' variable, if the item does validation, that is
				unless($item->always_passes_validation())
				{
					my $in = $item->get_item_name();
					push @valid_vars, cppvar('int8_t', $in.'ValidityError', $item->default_validation_state());
					push @valid_fns, "bool ${in}Valid() {return (m${in}ValidityError == WebAppForm::Valid);}";
				}
			}
		}
		
		# add in any condition variables for the conditional items
		my @itemconditions_vars;
		my $num_itemconditions = $#{$$self{'_item_conditions'}} + 1;
		for(my $i = 0; $i < $num_itemconditions; $i++)
		{
			push @itemconditions_vars, cppvar('bool','_ItemCondition'.$i)
		}

		# create a C++ class which represents the form
		my $webapp = $self->get_webapp();
		my $data_class = CppDataClass->new($webapp->get_webapp_name().'Form'.ucfirst($$self{'FormName'}),
			$user_validation_is_error_generating?'WebAppFormCustomErrors':'WebAppForm',
			@object_vars, @valid_vars, @itemconditions_vars, cppvar('bool _FormValid false'), cppvar('bool _WasSubmitted false'));
		# don't write Set() functions -- this is read only
		$data_class->set_option('ReadOnly', 1);
		# add extra bits of data and functions
		$data_class->add_declarations(CppDataClass::PUBLIC,
			$user_validation_is_error_generating
				?'bool FormValid() {return Get_FormValid() && !HaveErrorText();}'
				:'bool FormValid() {return Get_FormValid();}',
			'bool WasSubmitted() {return Get_WasSubmitted();}',
			'void SetFormToInvalid() {m_FormValid = false;}',
			'void SetFormToUnsubmitted() {m_WasSubmitted = false; m_FormValid = false;}',
			@valid_fns);
		# and a function to set the item conditions?
		if($num_itemconditions > 0)
		{
			my (@a,@i);
			for(my $i = 0; $i < $num_itemconditions; $i++)
			{
				push @a,"bool c$i";
				push @i,"m_ItemCondition$i = c$i;"
			}
			$data_class->add_declarations(CppDataClass::PUBLIC,
				'void _SetItemConditions('.join(', ',@a).') {'.join(' ',@i).'}'
			);
		}

		# store for later
		$$self{'_data_class'} = $data_class;
		
		# register with the base class so things can access it
		$self->register_variable_namespace($$self{'FormName'}, $data_class->get_data_members_ref());
		
		# initialise everything else
		$self->write_subunits($output, $phase, $subphase);
		
		# stop there
		return;
	}

	my $data_class = $$self{'_data_class'};

	# write error display (if it hasn't been relocated)
	$self->write_error_display($output, $phase, $subphase)
		unless exists $$self{'_externally_displayed_errors'};

	# write stuff on various output phases
	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS)
	{
		# declare the variable, and get the contents set up from the request
		$output->write_code("\t".$data_class->get_class_name().' '.$$self{'FormName'}.";\n");
		# set up strings, if it's an error generating form
		if($user_validation_is_error_generating)
		{
			$output->write_code("\t".$$self{'FormName'}.".SetStrings("
				.WebAppFramework::Output::string_to_cpp_static_string($self->get_fragment('ErrorListSeparate'))
				.", PageTranslatedStrings);\n");
		}
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_PREPARE)
	{
		# feed in any item conditions
		if($#{$$self{'_item_conditions'}} >= 0)
		{
			$output->write_code($$self{'FormName'}.'._SetItemConditions('
				.join(', ',@{$$self{'_item_conditions'}})
				.");\n")
		}

		# get data
		my $sav_args = $$self{'_args_callsetandvalidate'};
		$output->write_code("\t".$$self{'FormName'}.".SetAndValidate(rRequest$sav_args);\n");
		$output->write_code($$self{'PostSetAndValidateCode'}) if exists $$self{'PostSetAndValidateCode'};
		# output code to do the magic when the form is submitted?
		if(exists $$self{'HandleSubmission'})
		{
			$output->write_code("\tif(".$$self{'FormName'}.".WasSubmitted() && ".$$self{'FormName'}.".FormValid())\n\t{\n");
			if(ref($$self{'HandleSubmission'}))
			{
				# function which write the code
				&{$$self{'HandleSubmission'}}($self->get_root(), $self, $output)
			}
			else
			{
				# just plain text
				$output->write_code($$self{'HandleSubmission'})
			}
			$output->write_code("\t}\n");
		}
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# handle writing of sub units (which includes the form fields)

		# Output two possibilities for the form, one in the default state,
		# and another for when it needs to be redisplayed.
		
		$output->write_code("\tif(!".$$self{'FormName'}.".WasSubmitted())\n\t{\n");
		
			$self->write_form_start($output);
			$self->write_subunits($output, $phase, OUTPUT_SUB_PHASE_DEFAULT);
			$self->write_form_end($output);

		$output->write_code("\t}\n\telse\n\t{\n");

			$self->write_form_start($output);
			$self->write_subunits($output, $phase, OUTPUT_SUB_PHASE_REDISPLAY);
			$self->write_form_end($output);

		$output->write_code("\t}\n");
		
		# stop here, as the sub-units are already done
		return;
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_H_DECLARATION)
	{
		$output->write_code($data_class->generate_h());
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_CPP_CODE)
	{
		# get list of all items
		my @items = map {($_->isa('WebAppFramework::Unit::FormItem'))?$_:()} $self->flatten_heirarchy();

		# Write the data class boilerplate
		$output->write_code($data_class->generate_cpp());
		# write the function to read all the variables from the request
		my $dcn = $data_class->get_class_name();
		my $extraargs = $$self{'_args_setandvalidate'};
		my $fid = $$self{'_form_id'};
		$output->write_code(<<__E);
void ${dcn}::SetAndValidate(const HTTPRequest &rRequest$extraargs)
{
	HTTPRequest::Query_t query(rRequest.GetQuery());

	m_FormValid = false;
	m_WasSubmitted = (query.find("${fid}_s") != query.end());

	if(!m_WasSubmitted)
	{
		// Form wasn't submitted, don't attempt to process further
		return;
	}
	
__E

		# If conditions are set, mark such items as valid
		# This means that they will be kept marked as valid, unless a value actually does sneak through
		if(exists $$self{'_conditional_formitems'})
		{
			$output->write_code("// Conditionally displayed form items need to have default values changed\n");
			for(@items)
			{
				next unless $_->requires_acceptor_code();
				my $name = $_->get_item_name();
				if(exists ${$$self{'_conditional_formitems'}}{$name})
				{
					my $num = ${$$self{'_conditional_formitems'}}{$name};
					$output->write_code("if(!m_ItemCondition$num) {m${name}ValidityError = WebAppForm::Valid;}\n");
				}
			}
			$output->write_code("\n");
		}

		$output->write_code(<<__E);
	// Go through all form elements in turn, reading them into variables
	for(HTTPRequest::Query_t::const_iterator i(query.begin()); i != query.end(); ++i)
	{
		switch(WebApplication::FourCharStringToInt(i->first.c_str()))
		{
__E
		for(@items)
		{
			next unless $_->requires_acceptor_code();
			# write the item's accept code
			for my $id ($_->get_ids())
			{
				my $ide = WebAppFramework::element_to_string($id);
				$output->write_code("case $ide: // $id, ".$_->get_item_name()."\n{\n");
				$_->write_value_acceptance_code($output, cppvar('std::string', '(i->second)'),
					($_->always_passes_validation())?'':('m'.$_->get_item_name().'ValidityError'), $id);
				$output->write_code("}\nbreak;\n");
			}
		}

		$output->write_code(<<__E);
		default:
			// Ignore, not one of ours
			break;
		}
	}

__E

		if($has_user_validation)
		{
			my $args = $$self{'_args_callvalidate'};
			$output->write_code(<<__E);
	// Extra validation (app author implements this function)
	Validate($args);

__E
		}
	
		my @validation_items = map
			{
				($_->requires_acceptor_code() && !($_->always_passes_validation()))?'(m'.$_->get_item_name().'ValidityError == WebAppForm::Valid)':()
			} @items;
	
		if($#validation_items >= 0)
		{
			$output->write_code(<<__E);
	// Is the entire form valid?
	if(
__E
	$output->write_code('    '.join("\n    && ", @validation_items)."\n");
	$output->write_code(<<__E);
		)
	{
		m_FormValid = true;
	}
}
__E
		}
		else
		{
			# nothing to validate, just mark as valid and finish the function
			$output->write_code("m_FormValid = true;\n}\n");
		}
	}

	# Get sub-units to write their stuff too
	$self->write_subunits($output, $phase, $subphase);
}


sub conditional_items
{
	my ($self, $condition, @items) = @_;
	
	# condition is the condition for the fields to be shown.
	# %items key is fieldname or ref to unit, value is text or unit to replace it
	# with when it's not shown, undef if nothing to replace with.
	
	# Check variables are created
	if(!exists $$self{'_item_conditions'})
	{
		$$self{'_item_conditions'} = [];
		$$self{'_conditional_formitems'} = {};
	}
	
	# Work out the condition number, creating a new condition if necessary
	my $condition_num = 0;
	while($condition_num <= $#{$$self{'_item_conditions'}})
	{
		last if ${$$self{'_item_conditions'}}[$condition_num] eq $condition;
		$condition_num++;
	}
	if($condition_num >= $#{$$self{'_item_conditions'}})
	{
		push @{$$self{'_item_conditions'}},$condition
	}
	
	# Now run through and make the items actually conditional
	while($#items >= 0)
	{
		my ($i,$replacement);
		($i,$replacement,@items) = @items;
		if(ref($i))
		{
			# it's a reference to a unit, which must be scanned for other units
			# find all the names of the relevant form items
			my $items_found = 0;
			my $fn = 
				sub
				{
					my ($unit,$parent) = @_;
					if($unit->is_form_item())
					{
						$items_found++;
						my $name = $unit->get_item_name();
						${$$self{'_conditional_formitems'}}{$name} = $condition_num;
					}
					return 0;
				};
			&$fn($i,$self);
			$i->interate_through_subunits($fn);
			die "No items found within unit passed to conditional_items" unless $items_found > 0;
			# and build the appropraite conditional unit
			my $parent = $i->get_parent();
			my $conditional = WebAppFramework::Unit::OutputIf->new(
						'Condition' => $$self{'FormName'}.".Get_ItemCondition${condition_num}()",
						'@true' => $i,
					);
			$conditional->add_unit('false', $replacement) if defined $replacement;
			$parent->replace_unit($i, $conditional);
		}
		else
		{
			# it's the name of the form item, which must be found and replaced
			# with a conditional unit
			unless($self->interate_through_subunits(
					sub
					{
						my ($unit,$parent) = @_;
						if($unit->is_form_item())
						{
							if($unit->get_item_name() eq $i)
							{
								# This is the right item
								${$$self{'_conditional_formitems'}}{$i} = $condition_num;
								my $conditional = WebAppFramework::Unit::OutputIf->new(
											'Condition' => $$self{'FormName'}.".Get_ItemCondition${condition_num}()",
											'@true' => $unit,
										);
								$conditional->add_unit('false', $replacement) if defined $replacement;
								$parent->replace_unit($unit, $conditional);
								return 1;
							}
						}
						return 0;
					}
				)
			)
			{
				die "FormItem $i cannot be found"
			}
		}
	}
}


sub write_error_display
{	
	my ($self, $output, $phase, $subphase) = @_;
	
	return unless $phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT;
	
	# display of this conditional?
	my $disp_condition = $$self{'ErrorDisplayCondition'};
	return if $disp_condition eq 'false';
	if($disp_condition ne '')
	{
		$output->write_code("if($disp_condition)\n{\n");
	}
	
	# error generating data class?
	my $user_validation_is_error_generating = ($$self{'FormValidation'} eq 'errorgen');
	
	$output->write_code("\tif(".$$self{'FormName'}.".WasSubmitted() && !".$$self{'FormName'}.".FormValid())\n\t{\n// Display error message for form\n");
	$output->write_code("bool firstDone = false;\n") if exists $$self{'ExternalErrors'} || $user_validation_is_error_generating;	# avoid breaking up text
	$output->write_text($self->get_fragment('ErrorStart'));

	# want to do external errors?
	if($user_validation_is_error_generating || exists $$self{'ExternalErrors'})
	{
		# write bits of error message	
		$output->write_text($self->get_fragment('ErrorListStart'));
	}
	
	if($user_validation_is_error_generating)
	{
		# will always be first, so can skip the writing of the separator
		$output->write_code('if('.$$self{'FormName'}.".HaveErrorText())\n{\n");
		$output->write_code("firstDone = true;\n");
		$output->write_code("rResponse.WriteString(".$$self{'FormName'}.".GetErrorText());\n");		
		$output->write_code("}\n");
	}
	
	if(exists $$self{'ExternalErrors'})
	{
		# get list of all items, in form order
		my @items = map {($_->isa('WebAppFramework::Unit::FormItem'))?$_:()} $self->flatten_heirarchy();
		for(@items)
		{
			# some don't have data or always passes validation
			next unless $_->requires_acceptor_code();
			next if $_->always_passes_validation();
	
			# code checks to see if it's valid, writes a separator if it's not the first in the list
			$output->write_code("if(!".$self->get_is_item_valid_expression($_).")\n{\nif(firstDone)\n{\n");
			$output->write_text($self->get_fragment('ErrorListSeparate'));
			$output->write_code("}\nelse\n{\nfirstDone = true;\n}\n");
			# then gets the item to write it's own error message
			$_->write_validation_fail_message($output);
			# and finishes
			$output->write_code("}\n");
		}
	}
	
	if($user_validation_is_error_generating || exists $$self{'ExternalErrors'})
	{
		$output->write_text($self->get_fragment('ErrorListEnd'));
	}

	$output->write_code("}\n");

	# finish the overall conditional display block?
	if($disp_condition ne '')
	{
		$output->write_code("}\n");
	}
}

sub write_subunits
{
	my ($self, $output, $phase, $subphase) = @_;
	
	for(sort keys %{$$self{'_units'}})
	{
		${$$self{'_units'}}{$_}->write($output, $phase, $subphase);
	}
}

sub write_form_start
{
	my ($self,$output) = @_;
	$output->write_text('<form method="POST"><input type="hidden" name="'.$$self{'_form_id'}.'_s">');
}

sub write_form_end
{
	my ($self,$output) = @_;
	$output->write_text("</form>");
}

# given a FormItem, return an expression which evaulate to true if the
# field is valid
sub get_is_item_valid_expression
{
	my ($self,$item) = @_;
	
	if((!$item->requires_acceptor_code()) || $item->always_passes_validation())
	{
		die "Item type ".ref($item).", name ",$item->get_item_name().", does not provide acceptor code, or is always valid, cannot determine if entry is valid"
	}
	
	'('.$$self{'FormName'}.'.'.$item->get_item_name().'Valid())';
}

# get the name of the variable
sub get_item_validity_error
{
	my ($self,$item) = @_;
	
	if((!$item->requires_acceptor_code()) || $item->always_passes_validation())
	{
		die "Item type ".ref($item).", name ",$item->get_item_name().", does not provide acceptor code, or is always valid, cannot determine if entry is valid"
	}

	$$self{'FormName'}.'.Get'.$item->get_item_name().'ValidityError()';	
}

sub get_form_template_fragment
{
	my ($self,$frag_name) = @_;
	return $self->get_fragment($frag_name);
}

# whether or not error messages are displayed inline
sub inline_error_messages
{
	my ($self) = @_;
	return !exists $$self{'ExternalErrors'};
}

1;

