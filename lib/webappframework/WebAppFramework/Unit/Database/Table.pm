package WebAppFramework::Unit::Database::Table;
use strict;
use Symbol;
use base 'WebAppFramework::Unit::Database::DisplayQuery';
use WebAppFramework::ArgumentAdaptor;

# new() parameters:
#	Template => base name of template filename
#   FragmentsName => name of fragments to pull out of the file
#	Name => Name of the object (will be registered as a namespace)
#	Query => Database::Query object which should be executed
#	Args => Arguments
#	HideFields => Array of fields to hide. (optional)

# Add units as Column_Before_X to add a column before query result 'X'
# Or Column_Last to add a column at the right hand side.
# Append _<text> to name to have multiple columns added.
# Have n_Heading to specify a heading unit for any column above, where n is the generated name.


sub write_header
{
	my ($self, $output, $phase, $subphase) = @_;

	# calculate columns and positions
	$self->ensure_layout_calculated();

	# Write top of table
	$self->write_fragment_text($output, $phase, $subphase, 'Begin');
	# Then the headings
	$self->write_fragment_text($output, $phase, $subphase, 'RowBegin');
	for(@{$$self{'_headings'}})
	{
		$self->write_fragment_text($output, $phase, $subphase, 'HeadingCellBegin');
		if(ref($_))
		{
			# a Unit, rather than text
			$_->write($output,$phase,$subphase);
		}
		else
		{
			# text, which needs translating
			$output->write_text_translated($_)
				if $phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT;
		}
		$self->write_fragment_text($output, $phase, $subphase, 'HeadingCellEnd');
	}
	$self->write_fragment_text($output, $phase, $subphase, 'RowEnd');
}


sub write_row
{
	my ($self, $output, $phase, $subphase) = @_;

	# calculate columns and positions
	$self->ensure_layout_calculated();

	# write the data rows
	$self->write_fragment_text($output, $phase, $subphase, 'RowBegin');

	for(@{$$self{'_data'}})
	{
		$self->write_fragment_text($output, $phase, $subphase, 'DataCellBegin');
		if(ref($_))
		{
			# a Unit, rather than a simple data member
			$_->write($output,$phase,$subphase);
		}
		else
		{
			# variable, which simply needs to be output
			$self->write_variable_text($output, $_)
				if $phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT;
		}
		$self->write_fragment_text($output, $phase, $subphase, 'DataCellEnd');
	}

	$self->write_fragment_text($output, $phase, $subphase, 'RowEnd');
}


sub write_footer
{
	my ($self, $output, $phase, $subphase) = @_;

	# finish table
	$self->write_fragment_text($output, $phase, $subphase, 'End');
}


sub get_required_fragments
{
	my ($self) = @_;
	return (
		WebAppFramework::Unit::Database::DisplayQuery::get_required_fragments($self),
		qw/Begin RowBegin HeadingCellBegin HeadingCellEnd DataCellBegin DataCellEnd RowEnd End/
	);
}

sub ensure_layout_calculated
{
	my ($self) = @_;
	return if exists $$self{'_headings'};

	# get a list of column units
	my @extra_columns = sort grep {m/\AColumn_/} keys %{$$self{'_units'}};
	my %extra_columns_used = ();
	
	# create a hash of fields to ignore
	my %ignore_field;
	if(exists $$self{'HideFields'})
	{
		$ignore_field{$_} = 1 for(@{$$self{'HideFields'}});
	}

	# build lists
	my $h = [];
	my $d = [];
	for($$self{'Query'}->get_results())
	{
		my $name = $_->name();
		
		# ignore?
		next if exists $ignore_field{$name};

		# Any columns preceding this unit?
		$self->add_extra_columns('Column_Before_'.$name, \@extra_columns, \%extra_columns_used, $h, $d);
		
		# Does data display unit exist?
		if(exists ${$$self{'_units'}}{$name.'_Display'})
		{
			# just use the unit
			push @$d,${$$self{'_units'}}{$name.'_Display'}
		}
		else
		{
			# use the page variable for this
			push @$d,$$self{'Name'}.'.'.$name
		}
		# Does heading unit exist?
		if(exists ${$$self{'_units'}}{$name.'_Heading'})
		{
			# just use the unit
			push @$h,${$$self{'_units'}}{$name.'_Heading'}
		}
		else
		{
			# make a default heading
			my $default_heading_text = $name;
			# put spaces before capital letters
			$default_heading_text =~ s/([A-Z])/ $1/g;
			$default_heading_text =~ s/\A //;
			# store
			push @$h,$default_heading_text
		}
	}
	
	# add in any extra columns which go at the end
	$self->add_extra_columns('Column_Last', \@extra_columns, \%extra_columns_used, $h, $d);

	# Warn about any unused colums
	for(@extra_columns)
	{
		unless(exists $extra_columns_used{$_})
		{
			print "WARNING: Column Unit $_ is unused in WebAppFramework::Unit::Database::Table\n";
		}
	}

	# store calculated layout
	$$self{'_headings'} = $h;
	$$self{'_data'} = $d;
}

sub add_extra_columns
{
	my ($self, $namebase, $extra_columns, $extra_columns_used, $h, $d) = @_;

	for(@$extra_columns)
	{
		next unless m/\A$namebase(_|\Z)/;
		next if m/_Heading\Z/;
		# got a unit which goes before this column... add it to the data display list
		push @$d,${$$self{'_units'}}{$_};
		# mark as used
		$$extra_columns_used{$_} = 1;
		# see if there's a heading for it
		if(exists ${$$self{'_units'}}{$_.'_Heading'})
		{
			# Yes, use that unit as the heading
			push @$h,${$$self{'_units'}}{$_.'_Heading'};
			# mark as used
			$$extra_columns_used{$_.'_Heading'} = 1;
		}
		else
		{
			# Use the blank string, as no heading is provided
			push @$h,''
		}
	}
}

1;
