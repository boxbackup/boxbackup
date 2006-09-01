package WebAppFramework::Unit::OutputIf;
use strict;
use base 'WebAppFramework::Unit';
use base 'Exporter';
use vars qw/@EXPORT/;
@EXPORT = qw(output_if);

# new() parameters:
#	Condition => Condition to evaluate.
#
# Place Units to be output when this is true at 'true', false at 'false'.

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
	
	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		# check names of positions
		for(keys %{$$self{'_units'}})
		{
			if($_ ne 'true' && $_ ne 'false')
			{
				die "Bad position '$_' in OutputIf unit"
			}
		}
	}

	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# output statement
		$output->write_code('if('.$$self{'Condition'}.")\n{\n");
	}
	
	# write true unit?
	if(exists ${$$self{'_units'}}{'true'})
	{
		${$$self{'_units'}}{'true'}->write($output, $phase, $subphase);
	}
	# write false unit?
	if(exists ${$$self{'_units'}}{'false'})
	{
		if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
		{
			$output->write_code("}\nelse\n{\n");
		}
		${$$self{'_units'}}{'false'}->write($output, $phase, $subphase);
	}
		
	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT)
	{
		# finish statement
		$output->write_code("}\n");
	}
}

# helper function, which returns a suitable output if without the hassle
# of writing out the constructor properly.
sub output_if
{
	my ($condition, $if_true, $if_false) = @_;
	
	my $unit = WebAppFramework::Unit::OutputIf->new('Condition' => $condition);
	$unit->add_unit('true', $if_true) if defined $if_true;
	$unit->add_unit('false', $if_false) if defined $if_false;
	$unit
}

1;

