package WebAppFramework::Unit::DataSource::StaticStrings;
use strict;
use base 'WebAppFramework::Unit::DataSource';
use vars '$_strings_count';
use WebAppFramework::Output;

# Bind to a static list of strings (strings will be embedded as constants).

# Parameters to new()
#	Strings => ref to array of strings (NOT translated)
#				 or a | separated list of strings (which WILL be translated)

# This mainly used by the WebAppFramework::Unit::FormItem::Choice unit to
# write out large numbers of choices more efficiently.

$_strings_count = 0;

sub new_postcreate
{
	my ($self) = @_;
	
	# make a name for the static strings variable which shouldn't collide with anything.
	$$self{'_varname'} = '_ds_StaticStrings_'.$_strings_count;
	++$_strings_count;
}

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
	
	# write out the strings in the declaraction phase, so there's only ever
	# one copy, regardless of how many times the thing is output. (for example, in forms)
	if($phase == WebAppFramework::Unit::PHASE_LANG_CPP_DECLARATION)
	{
		my $varname = $$self{'_varname'};
	
		my $strings_r = $$self{'Strings'};
		unless(ref($strings_r))
		{
			# for checking that the number of items is the same
			my @untranslated = split /\|/,$strings_r;
	
			# unless we've already got translated strings, translate and split...
			$strings_r = [split /\|/,($output->translate_text($strings_r))];
			
			# check number returned
			if($#untranslated != $#$strings_r)
			{
				die "In StaticStrings data source, number of translated strings does not match number of untranslated strings"
			}
		}
		
		# store max integer value for later
		$$self{'_max_int_value'} = $#$strings_r + 1;
	
		# setup various bits
		my $num_strings = $#$strings_r + 1;
	
		# Write strings
		$output->write_code("#define ${varname}__COUNT $num_strings\nstatic const char *${varname}[] = {\n");
		my $sep = '';
		my $list = '';
		for(@$strings_r)
		{
			$list .= $sep . WebAppFramework::Output::string_to_cpp_static_string($_);
			$sep = ",\n"
		}
		$output->write_code($list."\n};\n");
	}
}

# see base class
sub write_bound_item
{
	my ($self, $function, $output, $phase, $subphase) = @_;
	
	# write for loop
	my $varname = $$self{'_varname'};
	$output->write_code(<<__E);
		for(int32_t key = 0; key < ${varname}__COUNT; ++key)
		{
__E
		&$function('key', $varname.'[key]', $output, $phase, $subphase);
	$output->write_code(<<__E);
		}
__E
}


# see base class
sub get_integer_range
{
	my ($self) = @_;
	my $max_int;
	my $strings_r = $$self{'Strings'};
	if(ref($strings_r))
	{
		$max_int = $#$strings_r;
	}
	else
	{
		my @c = split /\|/,$strings_r;
		$max_int = $#c;
	}
	return (0,$max_int)
}

# see base class
sub string_is_trusted
{
	# since the strings are static, they are trusted.
	return 1;
}


1;

