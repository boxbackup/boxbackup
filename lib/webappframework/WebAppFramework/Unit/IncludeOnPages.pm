package WebAppFramework::Unit::IncludeOnPages;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';

# Unit which only outputs it's sub units on specific pages
# (on other pages, the sub-units are actually deleted)

# new() parameters:
#	Pages => list of pages the sub units should be included on
#	NotPages => list of pages the sub units should not be included on
# one or the other can be used, but not both

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# need to delete the units?
		my $delete_subunits = 1;
		my $current_page = $self->get_pagename();
		
		# parse parameters
		if(exists $$self{'Pages'} && exists $$self{'NotPages'})
		{
			die "WebAppFramework::Unit::IncludeOnPages has both Pages and NotPages specified. Not allowed!";
		}
		if(exists $$self{'Pages'})
		{
			for($self->list_to_array($$self{'Pages'}))
			{
				$delete_subunits = 0 if $current_page eq $_
			}
		}
		elsif(exists $$self{'NotPages'})
		{
			$delete_subunits = 0;
			for($self->list_to_array($$self{'NotPages'}))
			{
				$delete_subunits = 1 if $current_page eq $_
			}
		}
		else
		{
			die "WebAppFramework::Unit::IncludeOnPages does not have Pages or NotPages parameter specified.";
		}
		
		# delete any...
		if($delete_subunits)
		{
			$$self{'_units'} = {}
		}
	}
	
	# call base class
	WebAppFramework::Unit::write_unit(@_);
}

1;