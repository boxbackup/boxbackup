package WebAppFramework::Unit::Database::QueryObject;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use Database::Query;

# Unit to output a Database::Query written code

# new() parameters:
#	Query => Database::Query object, or specification
#	Where => Global, Page, or Language -- which file to write it in
#			(defaults to Page if not specified)


sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# ensure the query object is created
	$self->ensure_query();
	
	# default location to the page
	$$self{'Where'} = 'Page' unless exists $$self{'Where'};

	# write the code!
	if($$self{'Where'} eq 'Global')
	{
		if($phase == WebAppFramework::Unit::PHASE_GLOBAL_H_DECLARATION)
		{
			$output->write_code($$self{'Query'}->generate_h());
		}
		elsif($phase == WebAppFramework::Unit::PHASE_GLOBAL_CPP_CODE)
		{
			$output->write_code($$self{'Query'}->generate_cpp());
		}
	}
	elsif($$self{'Where'} eq 'Page')
	{
		if($phase == WebAppFramework::Unit::PHASE_MAIN_H_DECLARATION)
		{
			$output->write_code($$self{'Query'}->generate_h());
		}
		elsif($phase == WebAppFramework::Unit::PHASE_MAIN_CPP_DECLARATION)
		{
			$output->write_code($$self{'Query'}->generate_cpp());
		}
	}
	elsif($$self{'Where'} eq 'Language')
	{
		if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_DECLARATION)
		{
			$output->write_code($$self{'Query'}->generate_h());
			$output->write_code($$self{'Query'}->generate_cpp());
		}
	}
	else
	{
		die 'Where location for WebAppFramework::Unit::QueryObject "'.$$self{'Where'}.'" is not valid'
	}
}


sub get_required_headers_this
{
	my ($self, $type) = @_;

	if($$self{'Where'} eq 'Global')
	{
		if($type == WebAppFramework::Unit::HEADERS_GLOBAL_H_PROJECT)
		{
			return ('DatabaseQuery.h')
		}
	}
	else
	{
		if($type == WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)
		{
			return ('DatabaseQuery.h')
		}
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
