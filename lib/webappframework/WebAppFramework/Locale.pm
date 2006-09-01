package WebAppFramework::Locale;
use strict;

# Dervied classes must implement the 'get' subroutine, which
# returns the 'translated' string. This can either be text, or
# other information such as orderings.
#
# Note that some things are implemented by the Locale C++ object
# at runtime.


# Constructor
sub new
{
	my ($type, %params) = @_;
	my $self = {};
	bless $self, $type;
}

# Returns name of the local. By default, just returns the
# last element of the module type.
sub get_name
{
	my ($self) = @_;
	my $type = ref($self);
	$type =~ m/::([^:]+?)\Z/;
	$1
}

# Returns the C++ class name
sub get_cpp_classname
{
	my ($self) = @_;
	return 'WAFLocale_'.($self->get_name())
}

# Returns the C++ header file which needs to be included
sub get_cpp_include_name
{
	my ($self) = @_;
	return ($self->get_cpp_classname()).'.h'
}

1;
