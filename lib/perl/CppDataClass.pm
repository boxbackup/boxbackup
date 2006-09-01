package CppDataClass;
use strict;
use CppVariable;

use constant PUBLIC => 1;
use constant PRIVATE => 2;
use constant PROTECTED => 3;

# create a new class, from a class name and list of items
sub new
{
	my ($type, $name, $derived_from, @items) = @_;

	my $self = {};
	$$self{'classname'} = $name;
	$$self{'derived_from'} = $derived_from;
	$$self{'items'} = [@items];
	$$self{'declarations'} = [];
	$$self{'options'} = {};
	
	bless $self, $type;
	$self
}

# Set an option
# 	ReadOnly, boolean, should Set() accessors be generated?
#	PublicVars, boolean, should the vars be public?
sub set_option
{
	my ($self, $option, $value) = @_;
	
	my @allowed = qw/ReadOnly PublicVars/;
	my $found = 0;
	for(@allowed) {$found = 1 if $_ eq $option;}
	die "Option $option is not known" unless $found;
	
	${$$self{'options'}}{$option} = $value;
}

	
# adds functions -- use all syntax including last ; if appropraite
sub add_declarations
{
	my ($self, $decl_type, @declarations) = @_;

	for(@declarations)
	{
		push @{$$self{'declarations'}}, [$decl_type, $_]
	}
}

# return the class name
sub get_class_name
{
	my ($self) = @_;
	return $$self{'classname'}
}

# return a reference to an array of all the data members
sub get_data_members_ref
{
	my ($self) = @_;
	return $$self{'items'}
}

# generates the text for the .h file
sub generate_h
{
	my ($self) = @_;

	my $classname = $$self{'classname'};
	my $derivation = ($$self{'derived_from'} ne '')?(" : public ".$$self{'derived_from'}):'';

	my $h = <<__E;
class ${classname}$derivation
{
public:
	${classname}();
	${classname}(const ${classname} &rToCopy);
	${classname} &operator=(const ${classname} &rToCopy);
	~${classname}();
__E

	# add any extra public functions
	$h .= $self->generate_declarations(PUBLIC);
	
	# add data accessor functions
	for my $v (@{$$self{'items'}})
	{
		$h .= "\t".$v->fn_return_type().' Get'.$v->name().'() const {return m'.$v->name().";}\n"
	}
	# if not read only, generate set functions
	if(!${$$self{'options'}}{'ReadOnly'})
	{
		for my $v (@{$$self{'items'}})
		{
			$h .= "\tvoid Set".$v->name().'('.$v->fn_arg_type().' '.$v->name.') {m'.$v->name().' = '.$v->name().";}\n";
		}
	}
	
	# add any other private or protected functions
	my $priv = $self->generate_declarations(PRIVATE);
	if($priv ne '')
	{
		$h .= "private:\n".$priv;
	}
	my $prot = $self->generate_declarations(PROTECTED);
	if($prot ne '')
	{
		$h .= "protected:\n".$prot;
	}
	
	# output the variables
	$h .= "private:\n";
	for my $v (@{$$self{'items'}})
	{
		$h .= "\t".$v->type().' m'.$v->name().";\n"
	}	
	
	# final essential bit of syntax
	$h .= "};\n";
	
	# return the results
	$h
}

# generates the text for the .cpp file
sub generate_cpp
{
	my ($self) = @_;

	my $classname = $$self{'classname'};
	
	# Default constructor
	my $cpp = "${classname}::${classname}()";
	my @default_init;
	my $first = 1;
	for my $v (@{$$self{'items'}})
	{
		# does this value need an default initialiser?
		if($v->needs_default_initaliser())
		{
			# add them to the text out
			$cpp .= ($first)?"\n    : ":",\n      ";
			$cpp .= 'm'.$v->name().'('.$v->default_value_allow_type_default().')';
			$first = 0;
		}
	}
	$cpp .= "\n{\n}\n";
	
	my $base = $$self{'derived_from'};
	
	# Copy constructor
	$cpp .= "${classname}::${classname}(const ${classname} &rToCopy)";
	$first = "\n\t: ";
	if($base ne '')
	{
		$cpp .= "\n\t: $base(rToCopy)";
		$first = '';
	}
	for my $v (@{$$self{'items'}})
	{
		$cpp .= ($first ne '')?$first:",\n\t  ";
		$cpp .= 'm'.$v->name().'(rToCopy.m'.$v->name().')';
		$first = '';
	}
	$cpp .= "\n{\n}\n";
	
	# operator=
	$cpp .= "${classname} &${classname}::operator=(const ${classname} &rToCopy)\n{\n";
	if($base ne '')
	{
		$cpp .= "\t${base}::operator=(rToCopy);\n"
	}
	for my $v (@{$$self{'items'}})
	{
		$cpp .= "\tm".$v->name().' = rToCopy.m'.$v->name().";\n";
	}
	$cpp .= "\treturn *this;\n}\n";
	
	# destructor
	$cpp .= "${classname}::~${classname}()\n{\n}\n";

	# all done	
	$cpp
}

# internal. Generates a string containing function declarations
sub generate_declarations
{
	my ($self, $decl_type) = @_;
	
	my $r;
	
	for my $f (@{$$self{'declarations'}})
	{
		if($$f[0] == $decl_type)
		{
			$r .= "\t";
			$r .= $$f[1];
			$r .= "\n";
		}
	}
	
	$r
}

1;
