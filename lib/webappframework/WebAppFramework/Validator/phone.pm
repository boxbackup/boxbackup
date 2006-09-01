package WebAppFramework::Validator::phone;
use strict;
use base 'WebAppFramework::Validator';

sub generate_code
{
	my ($self, $var, $boolean_result) = @_;
	
	# check the var is of the right type
	if($var->type() ne 'std::string')
	{
		die "WebAppFramework::Validator::phone passed a non-std::string value to validate"
	}

	# name of this variable
	my $toValidate = $var->name();

	# Return the code
	return <<__E
		{
			std::string canonical;
			$boolean_result = WAFUtility::ValidatePhoneNumber($toValidate, canonical);
			if($boolean_result) {$toValidate = canonical;}
		}
__E
}

sub generate_default_error_message
{
	my ($self, $noun) = @_;
	$noun . ' must be a valid phone number'
}

1;
