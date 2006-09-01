package WebAppFramework::Validator::email;
use strict;
use base 'WebAppFramework::Validator';
use WebAppFramework::Unit;

sub get_required_headers_this
{
	my ($self, $system_headers) = @_;

	if(!$system_headers)
	{
		return ('ValidateEmailAddress.h')
	}
	
	()
}

sub generate_code
{
	my ($self, $var, $boolean_result) = @_;
	
	# check the var is of the right type
	if($var->type() ne 'std::string')
	{
		die "WebAppFramework::Validator::email passed a non-std::string value to validate"
	}

	# read the modifier
	my $do_lookup = 'true';
	if($$self{'_modifier'} eq 'no-lookup')
	{
		$do_lookup = 'false';
	}
	elsif($$self{'_modifier'} ne '')
	{
		die "WebAppFramework::Validator::email only accepts 'no-lookup' as a modifier"
	}

	# name of this variable
	my $toValidate = $var->name();

	# Return the code
	return <<__E
		{
			std::string canonical;
			$boolean_result = ValidateEmailAddress($toValidate, canonical, $do_lookup);
			$toValidate = canonical;
		}
__E
}

sub generate_default_error_message
{
	my ($self, $noun) = @_;
	$noun . ' must be a valid email address'
}

1;
