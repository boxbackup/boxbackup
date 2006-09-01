package WebAppFramework::Unit::Database::OnSubmitExecSQL;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use WebAppFramework::ArgumentAdaptor;
use Database::Query;
use CppVariable;
use vars qw/$_autoname_start/;
# new() parameters:
#	Query => Database::Query object which should be executed
#	Args => Override finding of arguments from the form object
#	RedirectTo => Page to redirect the user to after success
#	PreExecuteCode => Any additional code to write before the database query is run
#	PostExecuteCode => Any additional code to write after the database query is run

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# ensure the query object list is created
	$self->make_query_list();
	
	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# find the form
		my $form = $self;
		while(ref($form) ne 'WebAppFramework::Unit::Form')
		{
			die "No form can be found by OnSubmitExecSQL" if $form eq '';
			$form = $$form{'_parent'};
		}
		
		# auto increment needed to be output?
		my $auto_inc_count = 0;
		for (@{$$self{'_query_list'}})
		{
			if($_->has_autoincrement())
			{
				$auto_inc_count++
			}
		}
		
		# Check the link, if provided, for special values
		if(exists $$self{'RedirectTo'})
		{
			my $l = $$self{'RedirectTo'};
			for(my $n = 2; $n <= $#$l; $n += 2)
			{
				if($$l[$n] eq 'QUERY_AUTO_INCREMENT_VALUE')
				{
					# need to change this to a local variable which will be created
					$$l[$n] = cppvar('int32_t', 'autoIncrementValue');
					# check have something to use!
					unless($auto_inc_count == 1)
					{
						die "In OnSubmitExecSQL, redirect uses auto-inc value but query does not specify one, or multiple queries specify one"
					}
				}
			}
		}

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
					for my $q (@{$$self{'_query_list'}})
					{
						# Create an adaptor object
						my $adaptor = WebAppFramework::ArgumentAdaptor->new(
							'Target' => $q,
							'Source' => $form,
							'DefaultSourceObject' => $form->get('FormName'),
							'Args' => $$self{'Args'});

						my $query_name = $q->get_name();
						my $obj_name = $query_name.'_OnSubmit';
						$output->write_code($adaptor->generate_call($obj_name.'.Execute', ''));

						# Store auto inc value?
						if($q->has_autoincrement())
						{
							if($auto_inc_count == 1)
							{
								# can write generic auto-inc value
								$output->write_code("int32_t autoIncrementValue = $obj_name.InsertedValue();\n");
							}
							else
							{
								# use named auto-inc values
								$output->write_code("int32_t ${query_name}_autoIncrementValue = $obj_name.InsertedValue();\n");
							}
						}
					}
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
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_H_DECLARATION)
	{
		$output->write_code($_->generate_h()) for (@{$$self{'_query_list'}})
	}
	elsif($phase == WebAppFramework::Unit::PHASE_MAIN_CPP_CODE)
	{
		$output->write_code($_->generate_cpp()) for (@{$$self{'_query_list'}})
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS)
	{
		for (@{$$self{'_query_list'}})
		{
			my $q_name = $_->get_name();
			$output->write_code("$q_name ${q_name}_OnSubmit(mApplication.GetDatabaseConnection());\n");
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

sub make_query_list
{
	my ($self) = @_;

	return if exists $$self{'_query_list'};
	
	die "in OnSubitExecSQL, Query parameter not specified" unless exists $$self{'Query'};
	my $s = $$self{'Query'};
	
	my $r;
	if(ref($s) eq 'Database::Query')
	{
		$r = [$s]
	}
	elsif(ref($s) eq 'HASH')
	{
		$r = [$self->hash_ref_to_query($s)]
	}
	elsif(ref($s) eq 'ARRAY')
	{
		$r = [map {$self->hash_ref_to_query($_)} @$s]
	}
	else
	{
		die "in OnSubitExecSQL, Query parameter has value of an unrecognised type"
	}
	
	$$self{'_query_list'} = $r
}

$_autoname_start = 0;
sub hash_ref_to_query
{
	my ($self,$q) = @_;
	my %p = %$q;
	if(!exists $p{'Name'})
	{
		# build a name
		my $i = $_autoname_start++;
		my $page = $self->get_pagename();
		$p{'Name'} = 'oses_'.$page.$i;
	}
	Database::Query->new(%p)
}

1;
