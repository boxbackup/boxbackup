package Database::Query;
use strict;
use vars qw/@required_attributes %allowed_attributes %allowed_flags/;
use CppVariable;

@required_attributes = qw/Name Statement/;
%allowed_attributes = qw/Name 1 Statement 1 Parameters 1 Results 1 Flags 1 AutoIncrementValue 1/;
%allowed_flags = qw/SingleValue 1/;

sub new
{
	my $type = shift;
	my $self = {@_};
	
	# check everything required is there
	for(@required_attributes)
	{
		die "Attribute $_ not specified to Database::Query"
			unless exists $$self{$_}
	}
	# check nothing bad is there
	for(keys %$self)
	{
		die "Attribute $_ is not a valid attribute to Database::Query"
			unless exists $allowed_attributes{$_}
	}
	# parse and check flags
	if(exists $$self{'Flags'})
	{
		for(split /\s+/,$$self{'Flags'})
		{
			die "Flag $_ is not a valid flag for Database::Query"
				unless exists $allowed_flags{$_};

			# set flag
			$$self{'Flag_'.$_} = 1;
		}
	}
	# check name
	unless($$self{'Name'} =~ /\A[a-zA-Z0-9_]+\Z/)
	{
		die "Bad name given to Database::Query, must contain only [a-zA-Z0-9_]"
	}
	# check autoincrement value flags
	if(exists $$self{'AutoIncrementValue'})
	{
		if($$self{'AutoIncrementValue'} !~ m/\A\w+\s+\w+\Z/)
		{
			die "Bad AutoIncrementValue given to Database::Query, must be 'TableName ColumnName'"
		}
		if($$self{'Statement'} !~ m/\A\s*INSERT\s+INTO/i)
		{
			die "AutoIncrementValue given to Database::Query when the statement does not appear to be an insert statement"
		}
	}

	# Basic checks pass, move on to parsing the attributes
	my $n_params = 0;
	if(exists $$self{'Parameters'})
	{
		my $params = [];
		my $params_null = [];
		parse_vars_list($$self{'Parameters'}, $params, $params_null, 'Parameter');
		$$self{'_parameters'} = $params;
		$$self{'_parameters_null'} = $params_null;
		$n_params = $#$params + 1;
	}
	# and the results
	if(exists $$self{'Results'})
	{
		my $results = [];
		my $results_null = [];
		parse_vars_list($$self{'Results'}, $results, $results_null);
		$$self{'_results'} = $results;
		$$self{'_results_null'} = $results_null;
	}
	# and check that the parameters in the SQL are in order, and the right number
	my $last_insert_mark = 0;
	my $sql = $$self{'Statement'};
	while($sql =~ m/\$(\d+)/g)
	{
		if($1 != ($last_insert_mark + 1))
		{
			die "Insert marks are not in order and/or have missing numbers in $sql"
		}
		$last_insert_mark++;
	}
	die "Number of parmaters provided does not match the number of insert marks in $sql"
		unless $last_insert_mark == $n_params;

	bless $self;
	$self 
}

sub get_name
{
	my ($self) = @_;
	$$self{'Name'}
}

# get the results, as CppVariables
sub get_results
{
	my ($self) = @_;
	if(exists $$self{'_results'})
	{
		return (@{$$self{'_results'}})
	}
	# no results
	return ();
}

# is it based on the generic query ('runtime' as statement)
sub dervied_from_DatabaseQueryGeneric
{
	my ($self) = @_;
	$self->is_runtime_statement();
}

# -----------------------------------------------------------
# interface for WebAppFramework::ArgumentAdaptor
sub get_arguments
{
	my ($self) = @_;
	if(exists $$self{'_parameters'})
	{
		# return parameters
		return (@{$$self{'_parameters'}});
	}
	# return nothing, none set
	return ();
}
sub get_arguments_null
{
	my ($self) = @_;
	if(exists $$self{'_parameters_null'})
	{
		# return parameters
		return (@{$$self{'_parameters_null'}});
	}
	# return nothing, none set
	return ();
}
sub some_arguments_may_be_null
{
	# some may be null
	return 1;
}
# -----------------------------------------------------------

# is a runtime statement?
sub is_runtime_statement
{
	my ($self) = @_;
	return ($$self{'Statement'} eq 'runtime')
}

# got an auto increment value?
sub has_autoincrement
{
	my ($self) = @_;
	return exists $$self{'AutoIncrementValue'}
}

# null allowed strings are NULL? or blank entirely
sub parse_null_allowed
{
	my $v = $_[0];
	return 0 if $v eq '';
	return 1 if $v eq 'NULL?';
	die "Unknown 'null allowed' parameter '$v' passed to Database::Query, must be NULL? or blank."
}

sub parse_vars_list
{
	my ($list, $vars_out, $vars_null_out, $default_name) = @_;

	# split up and look at each param in turn
	my $num = 1;
	for(split /,\s*/,$list)
	{
		# make a cpp variable from it
		s/\A\s+//; s/\s+\Z//;
		my ($type,$name,$null_allowed) = split /\s+/,$_;
		if($type eq '')
		{
			die "Variable specified with no type to Database::Query, suprious final , maybe?"
		}
		# make default name if none specified
		if($name eq '')
		{
			if($default_name ne '')
			{
				$name = $default_name.$num;
			}
			else
			{
				die "Must specify a name for all values in $list for Database::Query"
			}
		}
		# check and establish null allowed
		$null_allowed = parse_null_allowed($null_allowed);

		# build a cpp variable from it and add info to arrays
		push @$vars_out,cppvar($type,$name);
		push @$vars_null_out,$null_allowed;
		
		$num++;
	}
}

sub generate_h
{
	my ($self) = @_;
	
	my $class_name = $$self{'Name'};
	# boilerplate class start, with execute function
	my ($execute_args,undef) = $self->execute_args();
	my $baseclass = ($self->is_runtime_statement())?'DatabaseQueryGeneric':'DatabaseQuery';
	my $constructor_argextra = ($self->is_runtime_statement())?', const char *pStatement, bool VendoriseStatement = false':'';
	my $h = <<__E;
class $class_name : public $baseclass
{
public:
	$class_name(DatabaseConnection &rConnection$constructor_argextra);
	~$class_name();
private:
	// no copying
	$class_name(const $class_name &);
	$class_name &operator=(const $class_name &);
public:
__E
	# execute functions only output if not a runtime statement
	unless($self->is_runtime_statement())
	{
		$h .= <<__E;
	// Execute function
	void Execute($execute_args);
__E
	}

	my ($need_do,$do_return_type,$do_value_source) = $self->do_function_needed();
	if($need_do)
	{
		my $args = 'DatabaseConnection &rConnection' . (($execute_args ne '')?', ':'') . $execute_args;
		$h .= "\t// static Do() one shot execute function\n";
		$h .= "\tstatic $do_return_type Do($args);\n";
	}

	# auto increment id function
	if(exists $$self{'AutoIncrementValue'})
	{
		$h .= "\t// Retrieve the inserted ID\n";
		$h .= "\tint32_t InsertedValue() {return mInsertedValue;}\n";
	}


	# then generate the results functions
	if(exists $$self{'_results'})
	{
		$h .= "\t// Field retrieval functions\n";
		my $col = 0;
		for my $r (@{$$self{'_results'}})
		{
			my $nm = $r->name();
			if($r->is_int_type())
			{
				$h .= <<__E;
	int32_t Get$nm() {return GetFieldInt($col);}
	void Get$nm(int32_t &rFieldOut) {GetFieldInt($col, rFieldOut);}
__E
			}
			elsif($r->is_string_type())
			{
				$h .= <<__E;
	std::string Get$nm() {return GetFieldString($col);}
	void Get$nm(std::string &rFieldOut) {GetFieldString($col, rFieldOut);}
__E
			}
			else
			{
				die "result ".$r->name()." doesn't appear to have string or integer type in Database::Query"
			}
			if(${$$self{'_results_null'}}[$col])
			{
				# generate an IsNull() column for this
				$h .= "\tbool Is${nm}Null() {return IsFieldNull($col);}\n";
			}
			
			$col++;
		}
	}

	# and then finish off the class
	unless($self->is_runtime_statement())
	{
		$h .= <<__E;
protected:
	virtual const char *GetSQLStatement();
	virtual bool StatementNeedsVendorisation();
__E
	}

	if(exists $$self{'AutoIncrementValue'})
	{
	$h .= <<__E;
private:
	int32_t mInsertedValue;
__E
	}

	$h .= <<__E;
};
__E

	# lovely!
	$h
}

sub generate_cpp
{
	my ($self) = @_;
	
	my $class_name = $$self{'Name'};
	# boilerplate class start, with execute function
	my ($execute_args,$pass_on_args) = $self->execute_args();
	my $sql = $$self{'Statement'};
	$sql =~ tr/\t\n/  /;
	$sql =~ s/\"/\\\"/g;
	my $insertid_init = (exists $$self{'AutoIncrementValue'})?",\n\t  mInsertedValue(-1)":'';
	my $needsvendorisation = ($sql =~ m/\`/)?'true':'false';
	my $baseclass = 'DatabaseQuery';
	my ($constructor_argextra,$constructor_base_extra);
	if($self->is_runtime_statement())
	{
		$constructor_argextra = ', const char *pStatement, bool VendoriseStatement';
		$constructor_base_extra = ', pStatement, VendoriseStatement';
		$baseclass = 'DatabaseQueryGeneric';
	}
	my $cpp = <<__E;
${class_name}::${class_name}(DatabaseConnection &rConnection$constructor_argextra)
	: $baseclass(rConnection$constructor_base_extra)$insertid_init
{
}
${class_name}::~${class_name}()
{
}
__E
	unless($self->is_runtime_statement())
	{
		$cpp .= <<__E;
const char *${class_name}::GetSQLStatement()
{
	return "$sql";
}
bool ${class_name}::StatementNeedsVendorisation()
{
	return $needsvendorisation;
}
void ${class_name}::Execute($execute_args)
{
__E
	
		unless(exists $$self{'_parameters'})
		{
			# no parameters, just use basic execute function
			$cpp .= "\tDatabaseQuery::Execute(0, 0, 0);\n";
		}
		else
		{
			# marshall parameters, then call base class		
			my $params = $$self{'_parameters'};
			my $params_null = $$self{'_parameters_null'};
			my $n_params = $#$params + 1;
			my $marshalled_types = '';
			my $marshalled_params = '';
			
			for(my $p = 0; $p <= $#$params; $p++)
			{
				$marshalled_types .= ', ' unless $marshalled_types eq '';
				$marshalled_params .= ', ' unless $marshalled_params eq '';
				
				my $nm = $$params[$p]->name();
				if($$params[$p]->is_int_type())
				{
					$marshalled_types .= 'Database::Type_Int32';
					$marshalled_params .= ($$params_null[$p])?$nm:'&'.$nm;
				}
				elsif($$params[$p]->is_string_type())
				{
					$marshalled_types .= 'Database::Type_String';
					$marshalled_params .= ($$params_null[$p])?"(($nm == 0)?(0):($nm->c_str()))":$nm.'.c_str()';
				}
			}
			
			# write code
			$cpp .= <<__E;
	static const Database::FieldType_t parameterTypes[$n_params]
			= {$marshalled_types};
	const void *parameters[$n_params]
			= {$marshalled_params};
	DatabaseQuery::Execute($n_params, parameterTypes, parameters);
__E
		}
	
		# get insert value?
		if(exists $$self{'AutoIncrementValue'})
		{
			my ($tablename,$columnname) = split /\s+/,$$self{'AutoIncrementValue'};
			$cpp .= qq!\tmInsertedValue = GetConnection().GetLastAutoIncrementValue("$tablename", "$columnname");\n!;
		}
	
		# finish function
		$cpp .= <<__E;
}
__E
	}

	# write a Do function?
	my ($need_do,$do_return_type,$do_value_source) = $self->do_function_needed();
	if($need_do)
	{
		my $args = 'DatabaseConnection &rConnection' . (($execute_args ne '')?', ':'') . $execute_args;
		$cpp .= <<__E;
$do_return_type ${class_name}::Do($args)
{
	${class_name} query(rConnection);
	query.Execute($pass_on_args);
	return $do_value_source;
}
__E
	}

	#lovely
	$cpp
}

# returns (boolean, return type, result source)
sub do_function_needed
{
	my ($self) = @_;

	if(exists $$self{'Flag_SingleValue'})
	{
		# check for single result being specified properly
		if(!exists $$self{'_results'} || $#{$$self{'_results'}} != 0)
		{
			die "In query, when SingleValue flag is specified, one and only one result must be specified";
		}
		# work out values
		my $single_res = ${$$self{'_results'}}[0];
		my $return_type = ($single_res->is_int_type())?'int32_t':'std::string';
		my $fntype = ($single_res->is_int_type())?'Int':'String';
		return (1, $return_type, "query.GetSingleValue$fntype()")
	}

	if(exists $$self{'AutoIncrementValue'})
	{
		return (1, 'int32_t', 'query.mInsertedValue')
	}

	# no do function should be written
	(0, undef)
}

sub execute_args
{
	my ($self) = @_;

	return '' unless exists $$self{'_parameters'};

	my $params = $$self{'_parameters'};
	my $params_null = $$self{'_parameters_null'};
	my $a = '';
	my $b = '';
	for(my $p = 0; $p <= $#$params; $p++)
	{
		$a .= ', ' if $a ne '';
		$b .= ', ' if $b ne '';
		if($$params[$p]->is_int_type())
		{
			$a .= ($$params_null[$p])?'int32_t *':'int32_t ';
		}
		elsif($$params[$p]->is_string_type())
		{
			$a .= ($$params_null[$p])?'const std::string *':'const std::string ';
		}
		else
		{
			die "parameter ".$$params[$p]->name()." doesn't appear to have string or integer type in Database::Query"
		}
		$a .= $$params[$p]->name();
		$b .= $$params[$p]->name();
	}
	($a,$b)
}

1;

