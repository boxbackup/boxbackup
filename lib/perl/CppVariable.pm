package CppVariable;
use strict;
use base 'Exporter';
use vars qw/@EXPORT @_not_composite/;
@EXPORT = qw(cppvar);

use constant TYPE => 0;
use constant NAME => 1;
use constant DEFAULT_VALUE => 2;

@_not_composite = qw/bool float double/;

# Neat constructor function, exported
sub cppvar
{
	CppVariable->new(@_);
}

sub new
{
	my ($type, @params) = @_;
	
	my $self;
	
	# construct either from single param of space separated items, or a parameter list
	if($#params == 0)
	{
		if(ref($params[0]) eq 'CppVariable')
		{
			$self = [@{$params[0]}]
		}
		else
		{
			$self = [split /\s+/,$params[0]]
		}
	}
	else
	{
		$self = [@params]
	}
	
	# check length
	if($#$self < NAME || $#$self > DEFAULT_VALUE)
	{
		die "Invalid CppVariable constructor arguments"
	}
	
	bless $self, $type;
	$self
}

# Take a list of CppVariables and/or string representations, and
# return a list of CppVariables -- useful for intepreting lists
# as arguements to functions.
sub var_list
{
	return map {
		if(ref($_) eq 'CppVariable')
		{
			$_
		}
		else
		{
			cppvar($_)
		}
	} @_;
}

sub name
{
	my ($self) = @_;
	
	return $$self[NAME];
}

sub type
{
	my ($self) = @_;
	
	return $$self[TYPE];
}

sub fn_arg_type
{
	my ($self) = @_;
	
	if($self->is_composite_type())
	{
		return 'const '.$$self[TYPE].'&'
	}
	else
	{
		return $$self[TYPE]
	}
}

sub fn_return_type
{
	my ($self) = @_;
	
	# same logic as argument types
	return $self->fn_arg_type;
}

sub has_default
{
	my ($self) = @_;
	
	return ($$self[DEFAULT_VALUE] ne '')
}

# returns true if this needs an initialiser in a class constructor
# -- depends on whether it's composite, and whether it has a 
# default value
sub needs_default_initaliser
{
	my ($self) = @_;

	# non-composite types definately need default values
	return 1 if !$self->is_composite_type();

	# has it got a default value?
	return 1 if $$self[DEFAULT_VALUE] ne '';

	# otherwise it can do without
	return 0;
}

sub default_value
{
	my ($self) = @_;

	die "CppVariable ".$$self[TYPE]." ".$$self[NAME]." does not have a default value set, and needs one.\n"
		if $$self[DEFAULT_VALUE] eq '';
	
	return $$self[DEFAULT_VALUE]
}

sub default_value_allow_type_default
{
	my ($self) = @_;
	
	if($$self[DEFAULT_VALUE] eq '')
	{
		# no default value specified, return default for type
		return _default_value_for_type($$self[TYPE]);
	}

	return $$self[DEFAULT_VALUE]
}

sub is_composite_type
{
	my ($self) = @_;
	
	return _is_composite_type($$self[TYPE]);
}

sub _is_composite_type
{
	my ($type) = @_;

	# is it a natural type?
	return 0 if $type =~ m/\Au?int\d+_t\Z/;
	for(@_not_composite)
	{
		return 0 if $type eq $_;
	}
	
	return 1;
}

sub is_int_type
{
	my ($self) = @_;
	return $$self[TYPE] =~ m/\Aint(|(\d*)_t)\Z/
}

sub is_string_type
{
	my ($self) = @_;
	return 1 if $$self[TYPE] eq 'std::string';
	return 1 if $$self[TYPE] =~ m/\Aconst\s+char\s+\*\Z/;
	return 0
}

# returns a suitable default value for a given type
sub _default_value_for_type
{
	my ($type) = @_;
	
	if($type =~ m/int/)
	{
		return '0';
	}
	elsif($type eq 'bool')
	{
		return 'false';
	}
	elsif($type =~ m/string/i)
	{
		return '""';
	}

	die "Don't know about default value for type $type in _default_value_for_type()"
}

# Return a statement which converts this variable to the specified type.
sub convert_to
{
	my ($self, $to_type) = @_;

	# check for the easy case first
	return $$self[NAME] if $to_type eq $$self[TYPE];

	# work out the conversion function call
	my $conv_from_type = _is_composite_type($$self[TYPE])?"const ".$$self[TYPE]." &":$$self[TYPE];
	return 'BoxConvert::Convert<'.$to_type.", $conv_from_type>(".$$self[NAME].")"
}

# Use this function when you have a variable, an another variable, and you
# want to convert that other variable into a type suitable for assigning
# to this variable.
sub convert_from
{
	my ($self, @a) = @_;
	
	my $from = cppvar(@a);
	my ($from_type, $from_value) = ($from->type(),$from->name());

	# check for the easy case first
	return $from_value if $from_type eq $$self[TYPE];

	# work out the conversion function call
	my $conv_from_type = _is_composite_type($from_type)?"const $from_type &":$from_type;
	return 'BoxConvert::Convert<'.$$self[TYPE].", $conv_from_type>($from_value)"
}

1;

