package WebAppFramework::Unit;
use strict;
use vars qw/%_defaults %WAF_fns %_phase_names/;

# psudeo phase -- no output should be made!
# Happens once after the page has been completely set up / before global files are output
use constant PHASE_INITIALISE			=> 0;

# Define constants first
use constant PHASE_LANG_CPP__BEGIN 			=> 1;
use constant PHASE_LANG_CPP_DECLARATION 	=> 1;
use constant PHASE_LANG_CPP_HANDLE_VARS 	=> 2;
use constant PHASE_LANG_CPP_HANDLE_SECURITY	=> 3;
use constant PHASE_LANG_CPP_HANDLE_PREPARE	=> 4;
use constant PHASE_LANG_CPP_HANDLE_OUTPUT	=> 5;
use constant PHASE_LANG_CPP_FINISH			=> 6;
use constant PHASE_LANG_CPP__END			=> 6;

# Form module uses phases in range 80 -- 99

use constant PHASE_MAIN_CPP__BEGIN		=> 100;
use constant PHASE_MAIN_CPP_DECLARATION => 100;
use constant PHASE_MAIN_CPP_CODE		=> 101;
use constant PHASE_MAIN_CPP__END		=> 101;

use constant PHASE_MAIN_H__BEGIN 		=> 200;
use constant PHASE_MAIN_H_DECLARATION	=> 200;
use constant PHASE_MAIN_H__END			=> 200;

# global page
use constant PHASE_GLOBAL_CPP__BEGIN	=> 300;
use constant PHASE_GLOBAL_CPP_DECLARATION => 300;
use constant PHASE_GLOBAL_CPP_CODE		=> 301;
use constant PHASE_GLOBAL_CPP__END		=> 301;

use constant PHASE_GLOBAL_H__BEGIN 		=> 400;
use constant PHASE_GLOBAL_H_DECLARATION	=> 400;
use constant PHASE_GLOBAL_H__END		=> 400;

sub is_global_phase {my ($self,$p) = @_; return ($p >= PHASE_GLOBAL_CPP__BEGIN && $p <= PHASE_GLOBAL_CPP__END) || ($p >= PHASE_GLOBAL_H__BEGIN && $p <= PHASE_GLOBAL_H__END)}

my %_phase_names = (
		'lang_declaration' => &PHASE_LANG_CPP_DECLARATION,
		'lang_vars' => &PHASE_LANG_CPP_HANDLE_VARS,
		'lang_security' => &PHASE_LANG_CPP_HANDLE_SECURITY,
		'lang_prepare' => &PHASE_LANG_CPP_HANDLE_PREPARE,
		'lang_output' => &PHASE_LANG_CPP_HANDLE_OUTPUT,
		'lang_finish' => &PHASE_LANG_CPP_FINISH,
		'main_declaration' => &PHASE_MAIN_CPP_DECLARATION,
		'main_code' => &PHASE_MAIN_CPP_CODE,
		'main_h' => &PHASE_MAIN_H_DECLARATION,
		'global_declaration' => &PHASE_GLOBAL_CPP_DECLARATION,
		'global_code' => &PHASE_GLOBAL_CPP_CODE,
		'global_h' => &PHASE_GLOBAL_H_DECLARATION
	);

# header in the per-language pages
use constant HEADERS_SYSTEM 			=> 0;
use constant HEADERS_PROJECT			=> 1;
# headers for the page.h file
use constant HEADERS_PAGE_H_SYSTEM 		=> 2;
use constant HEADERS_PAGE_H_PROJECT		=> 3;
# headers for global.h file
use constant HEADERS_GLOBAL_H_SYSTEM 	=> 4;
use constant HEADERS_GLOBAL_H_PROJECT	=> 5;

# Then include other required modules
use CppVariable;
use WebAppFramework;
use WebAppFramework::Output;
use WebAppFramework::Unit::TranslatedText;
use WebAppFramework::Unit::LinkToPage;
use WebAppFramework::Unit::RawHTML;


sub new
{
	my ($type, %params) = @_;
	my $self = {};
	bless $self, $type;

	# set up initial structure
	$$self{'_pre_units'} = [];
	$$self{'_units'} = {};
	$$self{'_post_units'} = [];
	$$self{'_parameters'} = [];

	# set default attributes
	if(exists $_defaults{$type})
	{
		while(my ($k,$v) = each (%{$_defaults{$type}}))
		{
			$$self{$k} = $v
		}
	}

	# add given attributes, which may override defaults
	while(my ($k,$v) = each(%params))
	{
		if($k =~ m/\A_/)
		{
			die "Can't use _ to start parameter names for Units.";
		}
		elsif($k =~ m/\A\@(.+)\Z/)
		{
			# value is a unit (or text), store in appropraite place
			my $pos = $1;
			my $u = _preprocess_unit($v);
			${$$self{'_units'}}{$pos} = $u;
			$u->_set_parent($self);
		}
		else
		{
			$$self{$k} = $v;
		}
	}
	
	# Give the derived class a chance to do other work
	$self->new_postcreate();

	# return new object
	$self
}

# Function called after everything is setup by new()
sub new_postcreate
{
}

# set defaults for future new operations
sub set_defaults
{
	my ($type, %params) = @_;

	# create a defaults entry for this type if necessary
	$_defaults{$type} = {} unless exists $_defaults{$type};
	
	# entry to add attributes to
	my $d = $_defaults{$type};

	# Set all entries
	while(my ($k,$v) = each(%params))
	{
		if($k =~ m/\A_/)
		{
			die "Can't use _ to start parameter names for Units (while setting defaults for $type).";
		}
		$$d{$k} = $v;
	}
}

# adjust parameters after the new() event
sub set
{
	my ($self, $k, $v) = @_;
	die "Can't use _ to start parameter names for Units." if $k =~ m/\A_/;
	$$self{$k} = $v;
	return $self
}

# return parameters
sub get
{
	my ($self, $k) = @_;
	die "Parameter $k unknown" unless exists $$self{$k};
	$$self{$k}
}

sub param_exists
{
	my ($self, $k) = @_;
	return exists $$self{$k}
}

# used to pre-process a unit before adding it
sub _preprocess_unit
{
	my ($unit) = @_;
	if($unit =~ m/\A{([^}]+)}\Z/)
	{
		# single page variable, turn into a variable unit
		return WebAppFramework::Unit::Variable->new('Variable' => $1)
	}
	if(!ref($unit))
	{
		# text unit
		return WebAppFramework::Unit::TranslatedText->new('Text' => $unit);
	}
	
	# Normal unit
	$unit
}

# Add a Unit which will be output just before this one
sub add_pre_unit
{
	my ($self, $unit) = @_;
	$unit = _preprocess_unit($unit);
	
	unshift @{$$self{'_pre_units'}},$unit;
	$unit->_set_parent($self);
	$unit
}

# Add a unit at a given position
sub add_unit
{
	my ($self, $position, $unit) = @_;
	$unit = _preprocess_unit($unit);
	
	if(exists ${$$self{'_units'}}{$position})
	{
		die "Position $position is already in use"
	}
	
	${$$self{'_units'}}{$position} = $unit;
	$unit->_set_parent($self);
	$unit
}

# Get a unit at a specified position
sub get_unit
{
	my ($self, $position) = @_;
	${$$self{'_units'}}{$position}
}

# Add some translated text at a position, returning the created unit
sub add_text
{
	my ($self, $position, $text) = @_;

	my $unit = WebAppFramework::Unit::TranslatedText->new('Text' => $text);
	$self->add_unit($position, $unit);
	return $unit
}

# Add a Unit which will be output just after this one
sub add_post_unit
{
	my ($self, $unit) = @_;
	$unit = _preprocess_unit($unit);
	
	push @{$$self{'_post_units'}}, $unit;
	$unit->_set_parent($self);
	$unit
}

# replace a unit with another unit (cannot be text, must be a real unit)
sub replace_unit
{
	my ($self,$unit,$replace_with) = @_;

	# go through pre units
	my $pre = $$self{'_pre_units'};
	for(my $i = 0; $i <= $#$pre; $i++)
	{
		if($$pre[$i] == $unit)
		{
			$$pre[$i] = $replace_with;
			$replace_with->_set_parent($self);
			return;
		}
	}

	# this unit
	# paranoid reset each position -- necessary for some usage semantics
	keys %{$$self{'_units'}};
	while(my ($k,$v) = each(%{$$self{'_units'}}))
	{
		if($v == $unit)
		{
			${$$self{'_units'}}{$k} = $replace_with;
			$replace_with->_set_parent($self);
			# reset each() position
			keys %{$$self{'_units'}};
			return;
		}
	}

	# go through post-write units
	my $post = $$self{'_post_units'};
	for(my $i = 0; $i <= $#$post; $i++)
	{
		if($$post[$i] == $unit)
		{
			$$post[$i] = $replace_with;
			$replace_with->_set_parent($self);
			return;
		}
	}

	die "Unit $unit '".$$unit{'Name'}."' passed to replace_unit could not be found"
}


# Add pre-and post units to surround this with a link
sub link_to
{
	my ($self, @a) = @_;
	
	my $l = WebAppFramework::Unit::LinkToPage->new('Link' => [@a]);
	$self->add_pre_unit($l);
	$self->add_post_unit(WebAppFramework::Unit::RawHTML->new('HTML' => '</a>'));
	return $l
}

# Accepts a lists of parameters, which are either CppVariables
# or strings which can be turned into one.
# In practise, this is just a utility array, and the Unit does nothing
# with it. Used in for the top level page to contain a list of the
# parameters for the page (parameters are added to the root anyway)
sub add_parameters
{
	my ($self, @to_add) = @_;

	if (exists $$self{'_parent'}) {return $$self{'_parent'}->add_parameters(@to_add)}

	push @{$$self{'_parameters'}},CppVariable::var_list(@to_add);
	$self->register_variable_namespace('params',$$self{'_parameters'});
}

# returns the list of parameteres
sub get_parameters
{
	my ($self) = @_;
	if (exists $$self{'_parent'}) {return $$self{'_parent'}->get_parameters()}
	return @{$$self{'_parameters'}}
}

# returns a list of required headers, for that specific type
sub get_required_headers
{
	my ($self, $type) = @_;

	my @headers;
	
	# get headers from the other units
	for(@{$$self{'_pre_units'}})
	{
		push @headers,$_->get_required_headers($type);
	}
	for(@{$$self{'_post_units'}})
	{
		push @headers,$_->get_required_headers($type);
	}
	# and then the units in the
	for(values %{$$self{'_units'}})
	{
		push @headers,$_->get_required_headers($type);
	}

	# and finally, the ones for this unit itself
	push @headers,$self->get_required_headers_this($type);
	
	# deduplicate
	my %h;
	$h{$_} = 1 for(@headers);
	
	# return the de-duplicated list
	return keys %h;
}

# overload in unit implementation, to return any actual headers required
sub get_required_headers_this
{
	my ($self, $type) = @_;

	# return the empty list
	return ();
}

# write the output for the unit in the given phase.
# It shouldn't be necessary to override this.
sub write
{
	my ($self, $output, $phase, $subphase) = @_;

	# go through pre-write units
	for(@{$$self{'_pre_units'}})
	{
		$_->write($output, $phase, $subphase);
	}

	# write this unit
	$self->write_unit($output, $phase, $subphase);

	# go through post-write units
	for(@{$$self{'_post_units'}})
	{
		$_->write($output, $phase, $subphase);
	}
}

# Override this for the Unit implementation
# not a terribly useful implementation, as it just writes the contained
# units in, effectively, a random order
sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;
	
	for(values %{$$self{'_units'}})
	{
		$_->write($output, $phase, $subphase);
	}
}

# internal
sub _set_parent
{
	my ($self, $parent) = @_;
	
	die "Internal logic error: parent being set to own unit" if $self == $parent;
	
	$$self{'_parent'} = $parent;
}

# Get the parent Unit
sub get_parent
{
	my ($self) = @_;

	return $$self{'_parent'};
}

# Get the root Unit
sub get_root
{
	my ($self) = @_;

	return $$self{'_parent'}->get_root() if exists $$self{'_parent'};

	return $self;
}

# find the parent in the heirarchy of a given type
sub get_parent_of_type
{
	my ($self, $type) = @_;
	
	if(!exists $$self{'_parent'})
	{
		die "Parent of type $type not found"
	}
	
	if($$self{'_parent'}->isa($type))
	{
		return $$self{'_parent'}
	}
	
	return $$self{'_parent'}->get_parent_of_type($type)
}

# Register a set of variables to a namespace
# Can be a reference to an array or a function to generate the relevant CppVariable
sub register_variable_namespace
{
	my ($self, $name, $vars_ref) = @_;

	if (exists $$self{'_parent'}) {return $$self{'_parent'}->register_variable_namespace($name, $vars_ref)}
	
	$$self{'_vars'} = {} unless exists $$self{'_vars'};
	
	${$$self{'_vars'}}{$name} = $vars_ref;
}

sub get_variable
{
	my ($self, $varname) = @_;

	# If $varname is actually a CppVariable, return it now
	return $varname if ref($varname) eq 'CppVariable';

	# It's a textual reference to a CppVariable, return it processed into a CppVariable now
	return cppvar($1) if $varname =~ m/\A=(.+)\Z/;

	# It's a link specification, return an expression which evalutes to a std::string
	if($varname =~ m/\A\[(.+)\]\Z/)
	{
		# evalute link specification
		my @link_spec = eval('('.$1.')');
		if($@ ne '')
		{
			print "Could not parse link specification as embedded page variable:\n  Error: $@\n  Parameters: $1\n";
			exit(1);
		}
		# generate and return a variable containing the link spec
		return cppvar('std::string', $self->generate_page_address_expression(@link_spec))
	}

	# redirect to root
	if(exists $$self{'_parent'}) {return $$self{'_parent'}->get_variable($varname)}

	# split up such that namespaces can have .'s in them
	die "Variable name '$varname' doesn't specify namespace"
		unless $varname =~ m/\A(.+)\.([^\.]+)\Z/;
	my ($namespace,$varwithin) = ($1,$2);
	
	# is it the raw form data space?
	if($namespace eq 'formdata')
	{
		# an easy response
		return cppvar('std::string', qq`GetFormDataString(rRequest, std::string("$varwithin"))`)
	}
	
	# is it in the cookies namespace?
	if($namespace eq 'cookie')
	{
		return cppvar('std::string', qq`rRequest.GetCookie("$varwithin")`)
	}
	
	# is it in the config variables namespace?
	if($namespace eq 'config')
	{
		return cppvar('std::string', qq`GetConfigurationVariable("$varwithin")`)
	}
	
	# otherwise, find it in the registered namespaces
	die "Variable namespace $namespace doesn't exist"
		unless(exists ${$$self{'_vars'}}{$namespace});
	my $r = ${$$self{'_vars'}}{$namespace};

	if(ref($r) eq 'ARRAY')
	{
		# standard array reference
		for my $v (@$r)
		{
			if($v->name() eq $varwithin)
			{
				# build a variable to return
				return cppvar($v->type(), $namespace.'.Get'.$varwithin.'()');
			}
		}
	}
	elsif(ref($r) eq 'CODE')
	{
		# a function to find the variable
		my $var = &$r($namespace, $varwithin);
		return $var if defined $var;
	}
	else
	{
		die "Variable namespace $namespace has been registered with a ref of type ".ref($r).", which cannot be used."
	}
	
	die "Could not find variable $varname";
}


# given an Output object and Variable name, write the variable into the HTML
# If the string is of the form CONSTANT:value, then the value is written as a
# simple constant.
sub write_variable_text
{
	my ($self, $output, $varname) = @_;

	if($varname =~ m/\ACONSTANT:"?(.*)"?\Z/)
	{
		# just write the constant
		$output->write_text($1);
		return;
	}

	# It's a link specification, write it efficiently
	if($varname =~ m/\A\[(.+)\]\Z/)
	{
		# evalute link specification
		my @link_spec = eval('('.$1.')');
		if($@ ne '')
		{
			print "Could not parse link specification as embedded page variable in translated text:\n  Error: $@\n  Parameters: $1\n";
			exit(1);
		}
		# write out this variable as efficiently as possible
		$self->write_page_address($output, @link_spec);
		return;
	}

	# get the variable
	my $v = $self->get_variable($varname);
	
	# is a string? These need to be handled specially, to make sure
	# users don't do nasty things.
	if($v->type() eq 'std::string')
	{
		$output->write_code("\trResponse.WriteStringDefang(".$v->name().");\n")
	}
	else
	{
		# convert it to a string
		my $converted = $v->convert_to('std::string');
		$output->write_code("\trResponse.WriteString($converted);\n")
	}
}

# interpolation
sub write_interpolated_text
{
	my ($self,$output,$text) = @_;

	# Write it, perhaps interpolating with variables?
	if($text =~ m/{.+?}/)
	{
		# yes, output more complicated structure
		my @t = split /{(.+?)}/,$text;
		while($#t >= 0)
		{
			my ($tx,$var); ($tx,$var,@t) = @t;
			# write text
			$output->write_text($tx);
			# write variable?
			if($var ne '')
			{
				# use the base class to do it
				$self->write_variable_text($output, $var);
			}
		}
	}
	else
	{
		# no interpolation, just output simple text
		$output->write_text($text);
	}
}


sub dump_structure
{
	my ($self, $f, $pos, $level) = @_;

	# pre units
	my $posp = $pos; $posp =~ s/\A@//;
	for(@{$$self{'_pre_units'}})
	{
		$_->dump_structure($f, "pre-$posp", $level);
	}

	ref($self) =~ m/([^:]+)\Z/;
	
	print $f "\t" x $level, "$1 ($pos)\t\t";
	for my $k (sort(keys(%$self)))
	{
		my $v = $$self{$k};
		next if $k =~ m/\A_/;
		while(ref($v))
		{
			# try to find an extra something from it if it's not a straight scalar
			if(ref($v) eq 'ARRAY')
			{
				$v = $$v[0]
			}
			elsif(ref($v) eq 'HASH')
			{
				my (undef,$vv) = each %$v;
				# reset each position
				keys %$v;
				# store
				$v = $vv;
			}
			elsif(ref($v) eq 'SCALAR')
			{
				$v = $$v
			}
			else
			{
				# give up
				last
			}
		}
		# did it manage to find something printable?
		if(ref($v))
		{
			next;
		}
		# display it as a possible truncated string
		my $vp = $v;
		$vp =~ s/\s+/ /g;	# collapse whitespace
		if(length($vp) > 30)
		{
			print $f "$k=",substr($vp,0,30),"... ";
		}
		else
		{
			print $f "$k=$vp "
		}
	}
	print $f "\n";

	# Units within this one
	for my $p (sort(keys(%{$$self{'_units'}})))
	{
		${$$self{'_units'}}{$p}->dump_structure($f, '@'.$p, $level + 1);
	}
	
	# post units
	for(@{$$self{'_post_units'}})
	{
		$_->dump_structure($f, "post-$posp", $level);
	}
}

# call on root only
sub set_webapp
{
	my ($self, $webapp) = @_;
	$$self{'_webapp'} = $webapp;
}

sub get_webapp
{
	my ($self) = @_;
	if (exists $$self{'_parent'}) {return $$self{'_parent'}->get_webapp()}
	$$self{'_webapp'}
}

# call on root only
sub set_locale
{
	my ($self, $locale) = @_;
	$$self{'_locale'} = $locale;
}

sub get_locale
{
	my ($self) = @_;
	if (exists $$self{'_parent'}) {return $$self{'_parent'}->get_locale()}
	$$self{'_locale'}
}

# call on root only
sub set_pagename
{
	my ($self, $pagename) = @_;
	$$self{'_pagename'} = $pagename;
}

sub get_pagename
{
	my ($self) = @_;
	if (exists $$self{'_parent'}) {return $$self{'_parent'}->get_pagename()}
	$$self{'_pagename'}
}

# return all the units below this unit, essentially in a random order
sub flatten_heirarchy
{
	my ($self) = @_;

	my @flattened = @{$$self{'_pre_units'}};
	for(values %{$$self{'_units'}})
	{
		push @flattened, $_, $_->flatten_heirarchy()
	}
	push @flattened, @{$$self{'_post_units'}};

	@flattened
}

# return link to this page, with different parameters
sub write_page_address_this
{
	my ($self, $output, @a) = @_;
	$self->write_page_address($self, $output, $self->get_pagename(), @a);
}

sub write_page_address
{
	my ($self, $output, @a) = @_;
	
	# generate link data
	my @link = $self->_generate_page_address(1, @a); # 1 -- relative allowed

	# write out data, and strings
	my $is_constant = 1;
	for(@link)
	{
		if($is_constant)
		{
			$output->write_text($_);
		}
		else
		{
			$output->write_code("\trResponse.WriteString($_);\n");
		}
		$is_constant = !$is_constant;
	}
}

# return a expression which evaluates to a std::string of the link URL
sub generate_page_address_expression
{
	my ($self, @a) = @_;

	# generate link data
	my ($first_constant, @link) = $self->_generate_page_address(0, @a); # 0 -- relative not allowed!

	# create the initial starting point
	my $expression = 'std::string('.WebAppFramework::Output::string_to_cpp_static_string($first_constant, 2).')';

	# write out data, and strings
	my $is_constant = 0;
	for(@link)
	{
		if($is_constant)
		{
			$expression .= '+'.WebAppFramework::Output::string_to_cpp_static_string($_,2)
		}
		else
		{
			$expression .= '+'.$_
		}
		$is_constant = !$is_constant;
	}
	
	$expression
}

# return a link specification into something which can be embedded in code
# (can be used to avoid auto-generated code having to write nasty code
sub link_spec_to_WAF_code
{
	my ($self, $link_to, @link_params) = @_;
	
	# allow parameters to be passed as an anonymous array.
	($link_to, @link_params) = @{$link_to} if ref($link_to);
	
	my $params = '';
	while($#link_params >= 0)
	{
		my ($a,$b);
		($a,$b,@link_params) = @link_params;
		$params .= ",'$a'=>'$b'";
	}
	
	"WAF::Link('$link_to'$params)"
}

# return code suitable for a handler function, which redirects the user to another page
sub make_redirect_code
{
	my ($self, @a) = @_;
	
	my $link_expression = $self->generate_page_address_expression(@a);
	
	<<__E;
		{
			std::string uri($link_expression);
			rResponse.SetAsRedirect(uri.c_str());
		}
		// Finished generating response
		return true;
__E
}

sub _generate_page_address
{
	my ($self, $relative_allowed, $pagename, %param_values) = @_;
	
	# get the two parameter lists, the global parameters (which will be at the start
	# of both the parameter lists)
	my @this_param_list = $self->get_parameters();
	my @to_param_list = $self->get_webapp()->get_page_parameters($pagename);
	my @global_params = $self->get_webapp()->get_global_parameters();
	
	# try to fill in as many of the parameters as possible from the list
	# of parameter values supplied
	my @values;
	while(my ($p,$v) = each %param_values)
	{
		# search for the name in the to param list
		my $i;
		for($i = 0; $i <= $#to_param_list; $i++)
		{
			if($to_param_list[$i]->name() eq $p)
			{
				# store the value in the right place
				$values[$i] = $v;
				last;
			}
		}
		if($i > $#to_param_list)
		{
			die "Page $pagename has no parameter $p"
		}
	}
	
	# build the start of the address
	my $addr_start = $self->get_webapp()->get_url_base_for_page($pagename);
	# and where the adding of parameters should start (relative will omit some by increasing this number)
	my $first_param = 0;
	
	# if the too page has no parameters, stop now!
	if($#to_param_list < 0)
	{
		return ($addr_start)
	}
	
	# if the page names aren't the same, then try to fill missing
	# spaces in the page parameters from the defaults
	if($pagename ne $self->get_pagename())
	{
		for(my $d = $#global_params + 1; $d <= $#to_param_list; $d++)
		{
			if($values[$d] eq '')
			{
				# blank, copy in a default if it's available
				if($to_param_list[$d]->has_default())
				{
					$values[$d] = 'CONSTANT:'.$to_param_list[$d]->default_value();
				}
			}
		}
	}
	else
	{
		# link goes to the current page...
		if($relative_allowed)
		{
			# consider relative addressing -- how many values aren't specified at the beginning of the list?
			my $unspecified = 0;
			# NOTE: Correctly use < rather than <= in the statment below
			# so that the last value is never identified as a unspecified value.
			# This is so there's at least one .. in the results
			#                 \!/
			while($unspecified < $#to_param_list && $values[$unspecified] eq '')
			{
				$unspecified++
			}
			my $dotdots = join('/',('..') x ((($#to_param_list + 1) - $unspecified) - 1));
			if(length($dotdots) < length($addr_start))
			{
				# it's an output size optimisation! Use relative addressing
				$addr_start = $dotdots;
				$first_param = $unspecified;
			}
		}
	}
	
	# Now generate a list of items representing the text.
	my @link = ($addr_start);
	for(my $p = $first_param; $p <= $#to_param_list; $p++)
	{
		$link[$#link] .= '/';
	
		# is value not specified?
		if($values[$p] eq '')
		{
			# can this be obtained from the current values?
			if(($pagename eq $self->get_pagename()) || $p <= $#global_params)
			{
				# yes, because it's either on the same page, or one of the global ones.
				push @link, "rElements[".(WebAppFramework::URL_ELEMENTS_PARAMS_START + $p)."]",''
			}
			else
			{
				die "When generating link for page $pagename, parameter ".$to_param_list[$p]->name()." could not be determined"
			}
		}
		else
		{
			# convert the value to a string, and put it on the list
			if($values[$p] =~ m/\ACONSTANT:"?(.+)"?\Z/)
			{
				# constant value -- just add to previous text
				$link[$#link] .= $1
			}
			# local variable?
			elsif($values[$p] =~ m/\ALOCAL:"?(.+)"?\Z/)
			{
				# make a CppVariable out of it, push to link with blank text string
				push @link,cppvar($1)->convert_to('std::string'),'';
			}
			else
			{
				# non-constant
				my $v = $values[$p];
				unless(ref($v))
				{
					# Not a CppVariable, so assume it's a variable name
					$v = $self->get_variable($v)
				}
				# convert it to a std::string, add add a blank bit of text at the end
				push @link,$v->convert_to('std::string'),'';
			}
		}
	}
	# trim any blank bit of text from the end of this link
	pop @link if $link[$#link] eq '';
	# fix up relative links which are relative for the last parameter only
	if($addr_start eq '' && $link[0] =~ m!\A/!)
	{
		$link[0] =~ s!\A/!!;
	}
	
	# return the link data
	return @link
}

# support for a list of translated strings, which can be used if extra code
# requires 
sub add_translated_string
{
	my ($self, $name, $string) = @_;
	
	# only add this to the root
	if(exists $$self{'_parent'})
	{
		$$self{'_parent'}->add_translated_string($self, $name, $string);
		return;
	}
	
	# add to the list (creating the list if it doesn't already exist)
	$$self{'_translated_strings'} = [] unless exists $$self{'_translated_strings'};
	push @{$$self{'_translated_strings'}}, [$name, $string]
}

sub write_translated_strings
{
	my ($self, $output, $write_defines) = @_;
	
	# check stuff
	if(exists $$self{'_parent'})
	{
		$$self{'_parent'}->write_translated_strings($self, $write_defines);
		return;
	}
	return unless exists $$self{'_translated_strings'};
	
	my $a = $$self{'_translated_strings'};
	if($write_defines)
	{
		my $pn = uc($self->get_pagename());
		$output->write_code("\n");
		for(my $i = 0; $i <= $#$a; $i++)
		{
			$output->write_code("#define ${pn}_TRANSLATED_STRING_".${$$a[$i]}[0]."\t\t$i\n")
		}
		$output->write_code("\n");
	}
	else
	{
		$output->write_code("static const char *PageTranslatedStrings[] = {\n");
		for(my $i = 0; $i <= $#$a; $i++)
		{
			my $str = WebAppFramework::Output::string_to_cpp_static_string($output->translate_text(${$$a[$i]}[1]));
			$output->write_code($str.(($i == $#$a)?"\n":",\n"));
		}
		$output->write_code("};\n\n")
	}
}


# This belongs to the Form implementation. Used for finding fragement text
# within an arbitary heirarchy. Not nice to have it here, but OK.
sub get_form_template_fragment
{
	return undef;
}

# Another thing for the forms implementation
sub is_form_item
{
	return 0;
}


# Code pre-processing, implement the WAF::* functions!
sub preprocess_code
{
	my ($self, $code, $output) = @_;
	
	# find text to translate, and translate it
	$code =~ s/<<<<(.+?)>>>>/$self->preprocess_code_transtext($output,$1)/ges;

	# find all functions, and modify them
	$code =~ s/(\(([^\(\)]+?)\))?\s*WAF::(\w+?)\((.+?)\)/$self->preprocess_code_WAF($2,$3,$4)/ges;

	# return modified code
	$code
}

sub preprocess_code_transtext
{
	my ($self, $output, $text) = @_;
	
	# translate it first
	my $translated = $output->translate_text($text);
	
	# then split it and output the various bits
	my $o = '(';
	
	# split up to get translated bits
	my $done_first = 0;
	my @t = split /{(.+?)}/,$translated;
	while($#t >= 0)
	{
		my ($tx,$var); ($tx,$var,@t) = @t;
		if(!$done_first)
		{
			$done_first = 1;
			$o .= 'std::string('.WebAppFramework::Output::string_to_cpp_static_string($tx,0).')'
		}
		elsif($tx ne '')
		{
			$o .= "\n + ".WebAppFramework::Output::string_to_cpp_static_string($tx,0)
		}
		# get the variable
		if($var ne '')
		{
			$o .= "\n + ".$self->get_variable($var)->convert_to('std::string')
		}
	}
	
	$o .= ')';
	
	$o
}

my %WAF_fns = (
	'SetCookie' => \&preprocess_code_WAF_SetCookie,
	'Var' => \&preprocess_code_WAF_Var,
	'Link' => \&preprocess_code_WAF_Link
);

sub preprocess_code_WAF_SetCookie
{
	my ($self, $output_type, $cookie_name, $source_variable) = @_;
	
	return 'rResponse.SetCookie("'.$cookie_name.'", '.($self->get_variable($source_variable)->convert_to('std::string')).'.c_str(), "'
		.$self->get_webapp()->get_url_base().'")';
}

sub preprocess_code_WAF_Var
{
	my ($self, $output_type, $source_variable) = @_;

	# Get the variable
	my $var = $self->get_variable($source_variable);
	# By default, don't convert the variable
	if($output_type eq '')
	{
		# output it raw
		return $var->name();
	}
	else
	{
		# convert it
		return $var->convert_to($output_type);
	}

	return 'LOGICAL ERROR'
}

sub preprocess_code_WAF_Link
{
	my ($self, $output_type, @link_spec) = @_;

	# Check type is default or std::string if explicit
	unless($output_type eq '' || $output_type eq 'std::string')
	{
		die "WAF::Link cannot be cast to anything other than std::string\n";
	}

	# output text
	$self->generate_page_address_expression(@link_spec)
}

# change a psuedo-function into real output code
sub preprocess_code_WAF
{
	my ($self, $output_type, $function_name, $parameters) = @_;

	# Decode parameters
	my @params = eval('('.$parameters.')');
	if($@ ne '')
	{
		print "Could not parse parameters for WAF::$function_name:\n  Error: $@\n  Parameters: $parameters\n";
		exit(1);
	}
	
	# Check function exists
	unless(exists $WAF_fns{$function_name})
	{
		print "Psueodo function WAF::$function_name is not known.\n";
		exit(1);
	}
	
	# Return the processed function
	&{$WAF_fns{$function_name}}($self,$output_type,@params);
}

sub phase_name_to_number
{
	my ($self,$phase_name) = @_;
	die "Unknown phase name $phase_name" unless exists $_phase_names{$phase_name};
	$_phase_names{$phase_name}
}

sub call_in_default_page()
{
	my ($self) = @_;

	# call on pre-write units
	for(@{$$self{'_pre_units'}})
	{
		$_->in_default_page();
	}

	# call on this unit
	$self->in_default_page();

	# call on post-write units
	for(@{$$self{'_post_units'}})
	{
		$_->in_default_page();
	}

}

# default implementation for above
sub in_default_page()
{
}

# utility function -- takes either an array of items
# or a space separated list, and returns an array
sub list_to_array
{
	my ($self,$r) = @_;
	if($r eq '')
	{
		return ()
	}
	elsif(ref($r) eq 'ARRAY')
	{
		return @$r;
	}
	elsif(!ref($r))
	{
		return split /\s+/,$r
	}
	else
	{
		die "Anonymous ref to array or scalar space separated list expected"
	}
}

# find the first unit of a given type (or partial type) which has specified parameters.
# for example, $page->find_unit('Form', 'Name' => 'login');
# returns undef if it cannot be found.
sub find_unit
{
	my ($self, $type, %params) = @_;

	UNIT: for my $u (@{$$self{'_pre_units'}}, values %{$$self{'_units'}}, @{$$self{'_post_units'}})
	{
		# try and find units within this
		my $ub = $u->find_unit($type, %params);
		return $ub if defined $ub;
		
		if(substr(ref($u), 0-length($type)) eq $type)
		{
			# type matches, make sure all arguments match
			while(my ($k,$v) = each(%params))
			{
				if($$u{$k} ne $v)
				{	
					# reset each position
					keys %params;
					next UNIT;
				}
			}
			
			# all good, return it
			return $u
		}
	}

	# nothing found
	return undef;
}

# iterate through all subunits. Calls the function supplied for
# each unit (NOT including this one) with args
#	ref to unit
#	ref to parent
#	position (undef if pre or post unit)
#	'pre','post',undef	depending on where it is.
# If the function returns a true value, the search is aborted.
# (use closures (anonymous sub {}) to pass other data into this function)
sub interate_through_subunits
{
	my ($self,$function) = @_;

	# pre units	
	
	# go through pre units
	for(@{$$self{'_pre_units'}})
	{
		return 1 if &$function($_, $self, undef, 'pre');
		return 1 if $_->interate_through_subunits($function);
	}

	# positioned sub-units
	keys %{$$self{'_units'}}; # necessary sometimes
	while(my ($k,$v) = each(%{$$self{'_units'}}))
	{
		if(&$function($v, $self, $k, undef)
			|| $v->interate_through_subunits($function))
		{
			# reset each() position
			keys %{$$self{'_units'}};
			return 1;
		}
	}

	# go through post-write units
	for(@{$$self{'_post_units'}})
	{
		return 1 if &$function($_, $self, undef, 'post');
		return 1 if $_->interate_through_subunits($function);
	}
	return 0;
}

1;
