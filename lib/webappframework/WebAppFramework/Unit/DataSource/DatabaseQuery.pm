package WebAppFramework::Unit::DataSource::DatabaseQuery;
use strict;
use base 'WebAppFramework::Unit::DataSource';
use Database::Query;
use CppVariable;

# Bind to data from a database query.
#
# Parameters:
# 	Query => database query to use, or an anonymous hash of the Query parameters.
#	Args => Arguments for query

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
}


# see base class
sub write_bound_item
{
	my ($self, $function, $output, $phase, $subphase) = @_;
	
	# ensure the query object is created
	$self->ensure_query();
	my $query = $$self{'Query'};
	my $typename = $query->get_name();

	my $adaptor = $$self{'_adaptor'};
	my $execute = $adaptor->generate_call('query.Execute', '');
	
	my @results = $query->get_results();
	die "For DataSource::DatabaseQuery, query must have exactly two results" unless $#results == 1;
	my $key = $results[0];
	my $string = $results[1];
	
	# write loop code
	$output->write_code(<<__E);
		{
			$typename query(mApplication.GetDatabaseConnection());
			$execute
			while(query.Next())
			{
__E
		&$function('query.Get'.$key->name().'()',
				cppvar($string->type(), 'query.Get'.$string->name().'()')->convert_to('std::string'),
				$output, $phase, $subphase);
	$output->write_code(<<__E);
			}
		}
__E
}


# see base class
sub key_is_integer
{
	my ($self) = @_;
	# check the type of the key...
	$self->ensure_query();
	my $query = $$self{'Query'};
	my @results = $query->get_results();
	return $results[0]->is_int_type();
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

