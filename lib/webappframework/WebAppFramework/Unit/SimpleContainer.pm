package WebAppFramework::Unit::SimpleContainer;
use strict;
use Symbol;
use base 'WebAppFramework::Unit';
use base 'Exporter';
use vars qw/@EXPORT/;
@EXPORT = qw(join_units);
use WebAppFramework::Unit::TranslatedText;

# Simple container unit, simply outputs the sub units in sort() order of their positions.
# Optionally separating them by an HTML fragment

# new parameters:
#	Separator => some HTML to separate the sub-units

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
	
	my @u = sort keys %{$$self{'_units'}};
	
	for(my $i = 0; $i <= $#u; $i++)
	{
		# sub unit
		${$$self{'_units'}}{$u[$i]}->write($output, $phase, $subphase);
		
		# separator
		if($i != $#u
			&& $phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT
			&& exists $$self{'Separator'})
		{
			$output->write_text($$self{'Separator'})
		}
	}
}

# helper function. First param is Separator, then others are either references
# to units, or text scalars which will be translated. Returns a filled
# SimpleContainer unit to use.
sub join_units
{
	my ($separator, @items) = @_;
	
	my $r = WebAppFramework::Unit::SimpleContainer->new('Separator' => $separator);
	my $pos = 0;
	for(@items)
	{
		my $loc = sprintf("%03d",$pos++);
		if(ref($_))
		{
			# Reference, so assume it's a unit
			$r->add_unit($loc,$_);
		}
		else
		{
			# Not a reference, so scalar text... add it translated
			$r->add_unit($loc, WebAppFramework::Unit::TranslatedText->new('Text' => $_));
		}
	}
	$r
}


1;

