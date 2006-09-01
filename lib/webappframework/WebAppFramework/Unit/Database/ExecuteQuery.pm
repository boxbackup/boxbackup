package WebAppFramework::Unit::Database::ExecuteQuery;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use WebAppFramework::ArgumentAdaptor;
use Database::Query;
use CppVariable;

# new() parameters:
#	Name => Name of the object (will be registered as a namespace)
#	Query => Database::Query object which should be executed
#	Args => Arguments
#	RedirectToOnNoRow => Page to redirect the user to if there's no row available (optional)

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# ensure the query object is created
	$self->ensure_query();
	
	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# Create an adaptor object
		my $adaptor = WebAppFramework::ArgumentAdaptor->new(
			'Target' => $$self{'Query'},
			'Source' => $self,
			'Args' => $$self{'Args'});
		# and store
		$$self{'_adaptor'} = $adaptor;
		
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
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS)
	{
		# create the object
		$output->write_code($$self{'Query'}->get_name() . ' ' . $$self{'Name'} .
				"(mApplication.GetDatabaseConnection());\n");
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_PREPARE)
	{	
		# execute the query
		my $adaptor = $$self{'_adaptor'};
		$output->write_code($adaptor->generate_call($$self{'Name'}.'.Execute', ''));
		$output->write_code($$self{'Name'}.".Next();\n");
		# handle the case where there's nothing there
		if(exists $$self{'RedirectToOnNoRow'})
		{
			$output->write_code("if(!".$$self{'Name'}.".HaveRow())\n{\n");
			$output->write_code($self->make_redirect_code(@{$$self{'RedirectToOnNoRow'}}));
			$output->write_code("}\n");
		}
	}
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

sub ensure_query()
{
	my ($self) = @_;
	if(ref($$self{'Query'}) ne 'Database::Query')
	{
		# create a database query object as the given data isn't such an object
		my %p = %{$$self{'Query'}};
		my $q = Database::Query->new(%p);
		$$self{'Query'} = $q;
	}
}

1;

