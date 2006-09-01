package WebAppFramework::Unit::TableContainer;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::FragmentsTemplate';

# new() parameters:
#	Template => base name of template filename
#   FragmentsName => name of fragments to pull out of the file

# Add units with names <x>_<y> to position elements.

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# Make sure the template has been loaded
	$self->ensure_template_loaded($output);

	# Assemble into the nice grid layout
	$self->ensure_assembled();
	my $grid = $$self{'_assembly'};
	my $grid_types = $$self{'_assembly_types'};

	# Output units in order
	$self->write_fragment_text($output, $phase, $subphase, 'Begin');
	for(my $y = 0; $y <= $$self{'_max_y'}; $y++)
	{
		$self->write_fragment_text($output, $phase, $subphase, 'RowBegin');
		for(my $x = 0; $x <= $$self{'_max_x'}; $x++)
		{
			# Write unit!
			if($$grid[$x][$y] ne '')
			{
				$self->write_fragment_text($output, $phase, $subphase, 'CellBegin'.$$grid_types[$x][$y]);
				
				$$grid[$x][$y]->write($output, $phase, $subphase);
				
				$self->write_fragment_text($output, $phase, $subphase, 'CellEnd'.$$grid_types[$x][$y]);
			}
			else
			{
				# write an empty cell
				$self->write_fragment_text($output, $phase, $subphase, 'EmptyCell');
			}
		}
		$self->write_fragment_text($output, $phase, $subphase, 'RowEnd');
	}
	$self->write_fragment_text($output, $phase, $subphase, 'End');
}

sub get_required_fragments
{
	return qw/Begin RowBegin CellBegin CellEnd RowEnd EmptyCell End/;
}

sub ensure_assembled
{
	my ($self) = @_;

	# already done the work?
	return if exists $$self{'_assembly'};

	# Set up vars
	my $max_x = 0;
	my $max_y = 0;
	my $grid = [[]];
	my $grid_types = [[]];
	
	# process the stored units, one by one
	while(my ($position,$unit) = each %{$$self{'_units'}})
	{
		# match position to find x and y location
		unless($position =~ m/\A(\d+)_(\d+)(|_(\w+))\Z/)
		{
			die "Invalid position $position in TableContainer"
		}
		my ($x,$y,$type) = ($1,$2,$4);
		
		# store in grid
		$$grid[$x][$y] = $unit;
		
		# handle it being a different type to the default
		if($type ne '')
		{
			$$grid_types[$x][$y] = '-'.$type;
			$self->check_fragment_exists('CellBegin-'.$type);
			$self->check_fragment_exists('CellEnd-'.$type);
		}
		
		# record max position
		$max_x = $x if $x > $max_x;
		$max_y = $y if $y > $max_y;
	}
	
	# store
	$$self{'_max_x'} = $max_x;
	$$self{'_max_y'} = $max_y;
	$$self{'_assembly'} = $grid;
	$$self{'_assembly_types'} = $grid_types;
}

1;

