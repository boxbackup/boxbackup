package WebAppFramework::Unit::Database::FormNewOrEdit;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use WebAppFramework::ArgumentAdaptor;
use Database::Query;
use CppVariable;
use vars qw/%_autoname_start/;

# new() parameters:
#	NewCondition => C++ expression evaluating to true when the form is set to create a new object
#	ReplaceFieldsInEdit => ['FieldName' => 'Replacement', ...] to replace fields when the form is in edit mode
#	QueryNew => query(s) to create the object
#	QueryRead => query(s) to read existing data about the object from the database
#	QueryUpdate => query(s) to update the object in the database
#	ArgsNew => For the New query(s), override finding of arguments from the form object
#	ArgsRead => For the Read query(s), override finding of arguments from the form object
#	ArgsUpdate => For the Update query(s), override finding of arguments from the form object
#	ReadQueryNamespace => If present, the read query is registered as the named namespace, for access via page variables (in edit mode only!)
#	ItemOverrideForUpdate => ['FieldName' => 'default', ...] to override then initial state for the update form
#	RedirectTo => Page to redirect the user to after success
#	RedirectOnNoReadResults => Page to redirect the use to if there are no results from the read
#	PreExecuteCode => Any additional code to write before the database query is run
#	PostExecuteCode => Any additional code to write after the database query is run

# The form this refers to must be a positioned sub-unit of this unit.
# The defaults in the items of the form should be set to those for the new version.
# Defaults will be modified accordingly.

# In the Query* parameters, the data is a Database::Query object which should be executed,
# a hash array of Query constructor args (names automatically generated if not specified),
# or a array of Database::Query or hash arrays to represent a list of queries which should
# be executed.

# If a New or Update query has an auto-increment value, then it will be made available
# in the local variable <name>_AutoIncrementValue where <name> is the given name of the
# query. It can therefore be accessed as a page variable like '=int32_t query_AutoIncrementValue'.

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_INITIALISE && !(exists $$self{'_initdone'}))
	{
		# find the form, which will be one of the sub units
		my $form;
		while(my ($k,$v) = each %{$$self{'_units'}})
		{
			die "Sub unit $k has unexpected type in FormNewOrEdit" unless ref($v) eq 'WebAppFramework::Unit::Form';
			die "Multiple subunits are not allowed for FormNewOrEdit" if $form ne '';
			$form = $v;
		}
		$$self{'_form'} = $form;
		my $form_name = $form->get_form_name();
		$$self{'_form_name'} = $form_name;
		
		# set conditional fields?
		if(exists $$self{'ItemOverrideForUpdate'})
		{
			$form->conditional_items($form_name.'_InNewMode', @{$$self{'ItemOverrideForUpdate'}});
		}
		
		# Build the lists of queries
		$self->make_query_list('New');
		$self->make_query_list('Read');
		$self->make_query_list('Update');

		# Register the read query as a namespace, if required
		if(exists $$self{'ReadQueryNamespace'})
		{
			$self->register_variable_namespace($$self{'ReadQueryNamespace'},
				sub
				{
					my ($namespace,$var) = @_;
					$self->find_in_read_queries($var);
				}
			);
		}

		# Scan the form for form items, and modify the defaults
		$form->interate_through_subunits(
			sub
			{
				my ($unit,$parent) = @_;
				if($unit->is_form_item())
				{
					# Unit is a form item, set a filter for the default value
					$unit->set_default_value_filter(
						sub
						{
							my ($form_item,$default_value,$cpp_type) = @_;
							
							# Attempt to find the read value in the read queries
							my $read_value = $self->find_in_read_queries($form_item->get_item_name());
							return $default_value unless defined $read_value;
							
							# Turn this into a cpp variable
							my $def;
							if(!defined($default_value) || $default_value eq '')
							{
								$def = cppvar($cpp_type, ($cpp_type eq 'std::string')?'std::string("")':'0');
							}
							elsif($default_value =~ m/\ACONSTANT:(.*)\Z/)
							{
								$def = cppvar($cpp_type, ($cpp_type eq 'std::string')?('std::string("'.$1.'")'):$1);
							}
							else
							{
								$def = $form_item->get_variable($default_value);
							}
							
							# Now return a conditional expression
							return cppvar($cpp_type, '('.$form_name.'_InNewMode)?('
								. $def->convert_to($cpp_type)
								. '):(' . $read_value->convert_to($cpp_type) . ')');
						}
					);
				}
				return 0;
			}
		);

		# Add an on submit function to the form
		$form->set('HandleSubmission',
				sub
				{
					my ($page, $form, $output) = @_;
					
					# insert any additional code supplied by the user
					if(exists $$self{'PreExecuteCode'})
					{
						$output->write_code($$self{'PreExecuteCode'});
					}
					# write code to submit the queries
					$output->write_code("if(${form_name}_InNewMode)\n{\n");
					
					# write queries for the new mode
					for my $q (@{$$self{'_queryListNew'}})
					{
						$output->write_code("{\n".$q->get_name()." q(mApplication.GetDatabaseConnection());\n");
						my $adaptor = WebAppFramework::ArgumentAdaptor->new(
							'Target' => $q,
							'Source' => $form,
							'DefaultSourceObject' => $form_name,
							'Args' => $$self{'ArgsNew'});
						$output->write_code($adaptor->generate_call("q.Execute"));
						if($q->has_autoincrement)
						{
							$output->write_code($q->get_name()."_AutoIncrementValue = q.InsertedValue();\n");
						}
						$output->write_code("}\n");
					}
					
					# else clause
					$output->write_code("}\nelse\n{\n");
					
					# write queries for the update mode
					for my $q (@{$$self{'_queryListUpdate'}})
					{
						$output->write_code("{\n".$q->get_name()." q(mApplication.GetDatabaseConnection());\n");
						my $adaptor = WebAppFramework::ArgumentAdaptor->new(
							'Target' => $q,
							'Source' => $form,
							'DefaultSourceObject' => $form_name,
							'Args' => $$self{'ArgsUpdate'});
						$output->write_code($adaptor->generate_call("q.Execute"));
						if($q->has_autoincrement)
						{
							$output->write_code($q->get_name()."_AutoIncrementValue = q.InsertedValue();\n");
						}
						$output->write_code("}\n");
					}
					
					# finish if statement
					$output->write_code("}\n");

					# any more additional code from the user?
					if(exists $$self{'PostExecuteCode'})
					{
						$output->write_code($$self{'PostExecuteCode'});
					}					
					# redirect user?
					if(exists $$self{'RedirectTo'})
					{
						$output->write_code($self->make_redirect_code(@{$$self{'RedirectTo'}}));
					}
				}
			);
		
		# mark initialisaiton as done, as this phase may get called twice
		$$self{'_initdone'} = 1;
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_H_DECLARATION)
	{
		$self->output_queries($output, 1);
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_CPP_CODE)
	{
		$self->output_queries($output, 0);
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS)
	{
		# output definitions for all the read queries
		my $form_name = $$self{'_form_name'};
		my $n = 0;
		for(@{$$self{'_queryListRead'}})
		{
			$output->write_code($_->get_name()." ${form_name}_Read${n}(mApplication.GetDatabaseConnection());\n");
			$n++
		}
		# any autoincrement values?
		$self->define_autoinc_values($output, 'New');
		$self->define_autoinc_values($output, 'Update');
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_PREPARE)
	{
		# output the c++ expression for the new/edit condition
		my $form_name = $$self{'_form_name'};
		$output->write_code("bool ${form_name}_InNewMode = (".$$self{'NewCondition'}.");\n");
		# then, if the form hasn't been submitted, and we're not in new mode, read the data out
		# from the database
		$output->write_code(
			(exists $$self{'ReadQueryNamespace'})
				?("if(!${form_name}_InNewMode)\n{\n")	# query data required all the time if namespace is in use
				:("if(!${form_name}_InNewMode && !${form_name}.WasSubmitted())\n{\n"));
		$output->write_code("bool gotResults = true;\n") if exists $$self{'RedirectOnNoReadResults'};
		my $n = 0;
		for my $q (@{$$self{'_queryListRead'}})
		{
			my $adaptor = WebAppFramework::ArgumentAdaptor->new(
				'Target' => $q,
				'Source' => $self,	# use this unit as a source, doesn't really matter exactly where it comes from
				'Args' => $$self{'ArgsRead'});
			$output->write_code($adaptor->generate_call("${form_name}_Read${n}.Execute"));
			if(exists $$self{'RedirectOnNoReadResults'})
			{
				$output->write_code("if(!${form_name}_Read${n}.Next())\n{\ngotResults = false;\n}\n");
			}
			else
			{
				$output->write_code("${form_name}_Read${n}.Next();\n");
			}
			$n++;
		}
		if(exists $$self{'RedirectOnNoReadResults'})
		{
			$output->write_code("if(!gotResults)\n{\n");
			$output->write_code($self->make_redirect_code(@{$$self{'RedirectOnNoReadResults'}}));
			$output->write_code("}\n");
		}
		$output->write_code("}\n");
	}
	
	# ask the based class to write everything else
	WebAppFramework::Unit::write_unit($self, $output, $phase, $subphase);
}

sub make_query_list
{
	my ($self,$listname) = @_;

	return if exists $$self{'_queryList'.$listname};
	
	die "in FormNewOrEdit, Query$listname parameter not specified" unless exists $$self{'Query'.$listname};
	my $s = $$self{'Query'.$listname};
	
	my $r;
	if(ref($s) eq 'Database::Query')
	{
		$r = [$s]
	}
	elsif(ref($s) eq 'HASH')
	{
		$r = [$self->hash_ref_to_query($s,$listname)]
	}
	elsif(ref($s) eq 'ARRAY')
	{
		$r = [map {$self->hash_ref_to_query($_,$listname)} @$s]
	}
	else
	{
		die "in FormNewOrEdit, Query$listname parameter has value of an unrecognised type"
	}
	
	$$self{'_queryList'.$listname} = $r
}

%_autoname_start = ('New' => 0, 'Read' => 0, 'Update' => 0);
sub hash_ref_to_query
{
	my ($self,$q,$namestart) = @_;
	my %p = %$q;
	if(!exists $p{'Name'})
	{
		# build a name
		my $i = $_autoname_start{$namestart};
		$_autoname_start{$namestart}++;
		my $page = $self->get_pagename();
		$p{'Name'} = 'fnoe_'.$page.$namestart.$i;
	}
	Database::Query->new(%p)
}

sub output_queries
{
	my ($self,$output,$output_h) = @_;

	for my $n (qw/New Read Update/)
	{
		for my $q (@{$$self{'_queryList'.$n}})
		{
			$output->write_code($output_h
					?($q->generate_h())
					:($q->generate_cpp())
				)
		}
	}
}

sub define_autoinc_values
{
	my ($self,$output,$type) = @_;
	for my $q (@{$$self{'_queryList'.$type}})
	{
		if($q->has_autoincrement)
		{
			$output->write_code('int32_t '.$q->get_name()."_AutoIncrementValue = 0;\n");
		}
	}
}


sub find_in_read_queries
{
	my ($self,$name) = @_;
	
	my $n = 0;
	for my $q (@{$$self{'_queryListRead'}})
	{
		my @results = $q->get_results();
		for my $r (@results)
		{
			if($r->name() eq $name)
			{
				# found it, make a cpp variable for the value
				return cppvar($r->type(),  $$self{'_form_name'}."_Read${n}.Get".$name.'()');
			}
		}
		$n++;
	}
	
	# not found
	undef
}

sub get_required_headers_this
{
	my ($self, $type) = @_;
	if($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)
	{
		return ('DatabaseQuery.h')
	}
	return ()
}

1;
