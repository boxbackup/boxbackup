package WebAppFramework::ArgumentAdaptor;
use strict;

#	'Target' => $object,
#	'Source' => $unit,
#	'DefaultSourceObject' => 'objectname',
#	'Args' => ['name' => 'objectname.Name', 'name2' => $cppvariable]
#
#	$adaptor->generate_call('query', 'xyz = query.Do', 'rConnection'); # method name and handle return, preceeding arguments

#	$object must support...
#		get_arguments();	# array of CppVariables
#		some_arguments_may_be_null();	# true if some are null
#		get_arguments_null();	# only needs to be implemented if above fn is true

sub new
{
	my ($type, @params) = @_;

	my $self = {@params};
	bless $self;

	# return object
	$self
}


sub generate_call
{
	my ($self, $function_base, $additional_args) = @_;
	
	my @fn_args = $$self{'Target'}->get_arguments();
	my $has_nulls = 0;
	my @args_null;
	if($$self{'Target'}->some_arguments_may_be_null())
	{
		@args_null = $$self{'Target'}->get_arguments_null();
		for(@args_null) {$has_nulls = 1 if $_}
	}

	# for each of the arguments, locate the source for that variable
	my %arg_source_info;
	%arg_source_info = (@{$$self{'Args'}}) if exists $$self{'Args'} && ref($$self{'Args'});
	my @arg_source;
	for(my $n = 0; $n <= $#fn_args; $n++)
	{
		# is there a source specified for this argument?
		my $s;
		my $aname = $fn_args[$n]->name();
		if(exists $arg_source_info{$aname})
		{
			# yes, use it
			$s = $arg_source_info{$aname}; 
		}
		else
		{
			# no, generate a source page variable name using the default source
			unless(exists $$self{'DefaultSourceObject'})
			{
				die "Trying to find an argument source, $aname not sound in Args, and no DefaultSourceObject specified to ArgumentAdaptor"
			}
			$s = $$self{'DefaultSourceObject'} . '.' . $aname;
		}
		# add to array
		push @arg_source,$$self{'Source'}->get_variable($s);
	}

	# start generating the C++ -- need to convert any arguments which need to be passed by pointer?
	my $call;
	if($has_nulls)
	{
		# create local var of anything which may be null
		for(my $n = 0; $n <= $#fn_args; $n++)
		{
			if($args_null[$n])
			{
				my $comp = $fn_args[$n]->is_composite_type();
				my $ttype = $fn_args[$n]->type();
				$call .= $ttype .' a'.$n.($comp?'(':' = ').$arg_source[$n]->convert_to($ttype).($comp?");\n":";\n");
			}
		}
	}

	# function call
	$call .= $function_base . '(' . $additional_args . (($additional_args ne '' && $#fn_args >= 0)?', ':'');

	# generate arguments
	my $args;
	for(my $n = 0; $n <= $#fn_args; $n++)
	{
		$args .= ', ' if $args ne '';
		$args .= $arg_source[$n]->convert_to($fn_args[$n]->type())
	}

	# add args and terminate function call
	$call .= $args.");\n";

	if($has_nulls)
	{
		# indent nicely
		$call =~ s/^/\t/mg;
		# surround in a block
		$call = "{\n".$call."}\n";
	}
	$call
}

1;
