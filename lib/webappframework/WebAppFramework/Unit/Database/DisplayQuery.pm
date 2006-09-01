package WebAppFramework::Unit::Database::DisplayQuery;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FragmentsTemplate';
use WebAppFramework::ArgumentAdaptor;

# new() parameters:
#	Template => base name of template filename
#   FragmentsName => name of fragments to pull out of the file
#	Name => Name of the object (will be registered as a namespace)
#	Query => Database::Query object which should be executed
#	Args => Arguments
#	PreQueryCode => code to output just before the query is made
#	QueryCode => code to output to execute the query. Required if the class takes a runtime statement.
#	PostQueryCode => code to output just after the query is executed.

# Sub units will be displayed once per row in the returned table.

# This Unit is intended to be used as a base for other objects.
# Override write_header, write_row, write_footer for the basics!

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# initialisation
	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# Make sure the template has been loaded
		$self->ensure_template_loaded($output);

		# register the namespace
		$self->register_variable_namespace($$self{'Name'}, [$$self{'Query'}->get_results()]);
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_H_DECLARATION)
	{
		$output->write_code($$self{'Query'}->generate_h())
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_CPP_CODE)
	{
		$output->write_code($$self{'Query'}->generate_cpp())
	}

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# Write the header of the table
		$self->write_header($output, $phase, $subphase);

		# execute the query
		$output->write_code("{\n");
		$output->write_code($$self{'PreQueryCode'}) if exists $$self{'PreQueryCode'};
		if(exists $$self{'QueryCode'})
		{
			# Use the user supplied code to instantiate and execute the query
			my $query_code = $$self{'QueryCode'};
			$output->write_code($query_code);
			# check that it mentioned the object, just to warn the user
			my $nm = $$self{'Name'};
			if($query_code !~ /$nm/)
			{
				die "On DisplayQuery, QueryCode supplied does not mention '$nm', which is the required name of the query object. Check your code!"
			}
		}
		else
		{
			# check that the class isn't a runtime statment query
			if($$self{'Query'}->is_runtime_statement())
			{
				die "On DisplayQuery, if you use a Query with a runtime statement, then you must supply the QueryCode to execute it."
			}
		
			# generate the code for the query
			$output->write_code($$self{'Query'}->get_name() . ' ' . $$self{'Name'} .
					"(mApplication.GetDatabaseConnection());\n");
			my $adaptor = WebAppFramework::ArgumentAdaptor->new(
				'Target' => $$self{'Query'},
				'Source' => $self,
				'Args' => $$self{'Args'});
			$output->write_code($adaptor->generate_call($$self{'Name'}.'.Execute', ''));
		}
		$output->write_code($$self{'PostQueryCode'}) if exists $$self{'PostQueryCode'};
		# begin the display loop
		$output->write_code('while('.$$self{'Name'}.".Next())\n{\n");
	}	

	# Write the row
	$self->write_row($output, $phase, $subphase);

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# end loop and query block
		$output->write_code("}\n}\n");

		# Write the footer
		$self->write_footer($output, $phase, $subphase);
	}
}

# Derived objects can override this
# Called in HANDLE_OUTPUT phase only
sub write_header
{
	my ($self, $output, $phase, $subphase) = @_;
}

# Derived objects can override this
# Called in all phases
sub write_row
{
	my ($self, $output, $phase, $subphase) = @_;
	
	# Write all the sub units in order
	for(sort keys %{$$self{'_units'}})
	{
		# sub unit
		${$$self{'_units'}}{$_}->write($output, $phase, $subphase);
	}
}

# Derived objects can override this
# Called in HANDLE_OUTPUT phase only
sub write_footer
{
	my ($self, $output, $phase, $subphase) = @_;
}

# Derived objects can override this, but should always return the bits this returns!
sub get_required_fragments
{
	return ();
}

# Derived objects can override this, but should always return the bits this returns!
sub get_required_headers_this
{
	my ($self, $type) = @_;
	if($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)
	{
		my $query = $$self{'Query'};
		return ($query->dervied_from_DatabaseQueryGeneric())?('DatabaseQueryGeneric.h'):('DatabaseQuery.h');
	}
	return ()
}

1;
