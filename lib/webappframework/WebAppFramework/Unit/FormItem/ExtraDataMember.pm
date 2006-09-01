package WebAppFramework::Unit::FormItem::ExtraDataMember;
use strict;
use base 'WebAppFramework::Unit::FormItem';
use CppVariable;

# new() parameters:
#	Variable => CppVariable style specification of data member

sub new
{
	my ($type, @params) = @_;
	
	# construct and interpret variable specification
	my $self = WebAppFramework::Unit::new($type, @params);
	$$self{'_var'} = cppvar($$self{'Variable'});
	
	# Generate a name for the form
	$$self{'Name'} = $$self{'_var'}->name().'__extradatamember';

	$self
}

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
}

# by default, no form variable is used for submit buttons
# If the result should be reported, then the name followed by 'Clicked' is reported
sub get_form_variables
{
	my ($self) = @_;

	return (cppvar($$self{'Variable'}))
}

# no acceptor code should be written
sub requires_acceptor_code()
{
	my ($self) = @_;
	return 0;
}

# shouldn't ever display an error marker
sub disable_error_marker
{
	return 1;
}

# doesn't do any validation
sub always_passes_validation
{
	return 1
}

1;

