package WebAppFramework::Unit::DataSource::CppSTLContainer;
use strict;
use base 'WebAppFramework::Unit::DataSource';
use CppVariable;

# Bind to data in an STL container.
#
# Parameters:
# 	Container => page variable containing typed container variable
#	IgnoreBlank => if set, blank strings are ignored

# see base class
sub write_bound_item
{
	my ($self, $function, $output, $phase, $subphase) = @_;
	
	# ensure we know what we're going
	$self->setup();
	my $var = $self->get_variable($$self{'Container'});
	my $v_type = $var->type();
	my $v_name = $var->name();
	
	# for writing the item
	my $key;
	my $string;

	# write the loop
	if($$self{'_type'} eq 'vector')
	{
		$output->write_code(<<__E);
			{
				int32_t i = 0;
				for(i = 0; i < (int32_t)$v_name.size(); ++i)
				{
__E
		if(exists $$self{'IgnoreBlank'})
		{
			$output->write_code(<<__E);
					if(${v_name}[i].empty()) continue;
__E
		}
		$key = 'i';
		$string = cppvar($$self{'_string_type'}, $v_name.'[i]')->convert_to('std::string');
	}
	elsif($$self{'_type'} eq 'map')
	{
		$output->write_code(<<__E);
			{
				for(${v_type}::const_iterator i($v_name.begin()); i != $v_name.end(); ++i)
				{
__E
		$key = 'i->first';
		$string = cppvar($$self{'_string_type'}, 'i->second')->convert_to('std::string');
	}
	
	# write whatever's required to output the item
	&$function($key, $string, $output, $phase, $subphase);

	$output->write_code(<<__E);
			}
		}
__E
}



# see base class
sub key_is_integer
{
	my ($self) = @_;
	$self->setup();
	return $$self{'_key_is_int'};
}

# get all the necessary bits of information
sub setup
{
	my ($self) = @_;
	return if exists $$self{'_type'};
	
	my $var = $self->get_variable($$self{'Container'});
	my $type = $var->type();
	
	# what kind of type is it?
	if($type =~ m/vector\s*<\s*([^>]+)\s*>/)
	{
		$$self{'_type'} = 'vector';
		$$self{'_key_is_int'} = 'int';
		$$self{'_string_type'} = $1;
	}
	elsif($type =~ m/map\s*<\s*([^,]+)\s*,\s*([^>]+)\s*>/)
	{
		$$self{'_type'} = 'map';
		$$self{'_key_is_int'} = ($1 =~ m/int/);
		$$self{'_key_type'} = $1;
		$$self{'_string_type'} = $2;
	}
	else
	{
		die "Unknown container type $type for DataSource::CppSTLContainer\n";
	}
}

1;

