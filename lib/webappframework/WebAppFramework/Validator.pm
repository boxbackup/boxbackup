package WebAppFramework::Validator;
use strict;

# new() is always called from TextField with a single string as a modifer
# Other validators outside this system can use other schemes.
sub new
{
	my ($type, $modifer) = @_;
	my $self = {};
	bless $self, $type;

	# store modifer argument
	$$self{'_modifier'} = $modifer;

	# return object
	return $self
}


# Return a list of the headers this unit requires. $system_headers
# is a true value if system headers are being queried.
sub get_required_headers_this
{
	my ($self, $system_headers) = @_;

	# by default, return nothing
	()
}


# Generate the code to validate the input
# Args are:
#	CppVariable of the source of the data
#	Name of the variable of type bool which should be set to true if the data validates
# NOTE: Validators are allowed to modify the variable to set it to a more "canonical form",
# so it must be an actual variable, not a function call.
sub generate_code
{
	my ($self, $var, $boolean_result) = @_;
	
	die "WebAppFramework::Validator base class called to generate_code"
}


# Generate a default error message in the default language for when validation fails.
# The noun is the name of the item which has failed validation.
sub generate_default_error_message
{
	my ($self, $noun) = @_;
	
	die "WebAppFramework::Validator base class called to generate_default_error_message"
}


# Utility function to load a validator given a string of name(modifier)
sub load_builtin
{
	my ($specify) = @_;

	# split the specification into name and modifier
	my ($name,$modifier) = (undef,undef);
	if($specify =~ /\A\w+\Z/)
	{
		$name = $specify
	}
	elsif($specify =~ /\A(\w+)\((.*)\)\Z/)
	{
		$name = $1;
		$modifier = $2;
	}
	else
	{
		die "Invalid Validator specifier '$specify'\n"
	}

	# see if an appropraite builtin module can be loaded
	my $objtype = 'WebAppFramework::Validator::'.$name;
	my $validator;
	eval <<__E;
		use $objtype;
		\$validator = $objtype->new(\$modifier);
__E
	if($@)
	{
		die "Failed to automatically load builtin validator of type '$name' (specifier was '$specify')\n";
	}
	
	# return the loaded module
	$validator
}

1;
