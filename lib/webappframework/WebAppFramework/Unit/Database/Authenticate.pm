package WebAppFramework::Unit::Database::Authenticate;
use strict;
use base 'WebAppFramework::Unit';
use Database::Query;
use CppVariable;
use WebAppFramework::Unit::Code;

# new() parameters:
#	Name => Name of the object (will be registered as a namespace)
#	Query => Database::Query object which should be executed, or ref to hash array for constructor (which can omit 'Name')
#			First parameter must be the ID, second optional must be token
#	TokenColumn => Name of the column (in Results of the Query) which contains the token
#	CredentialsSource => Name of the variable containing the credentials (cookie, form param?)
#	RedirectToOnAuthFailure => Page to redirect the user to if there's no row available (optional)
#	DisableRedirectOnPages => List of pages to not do the above redirect
#	TokenFilter => 'MD5' to implement MD5 hashing (optional)
#	MD5SecretConfigVar => If MD5 token filter, the source of the secret for the filter

sub write_unit
{
	my ($self, $output, $phase, $subphase) = @_;

	# check this is in the page template, not just in a page
	die "Database::Authenticate must be added in the setup_page subroutine only."
		unless exists $$self{'_in_default_page'};

	if($phase == WebAppFramework::Unit::PHASE_INITIALISE)
	{
		for(qw/Name Query TokenColumn CredentialsSource/)
		{
			die "$_ not set for Database::Authenticate" unless exists $$self{$_}
		}
	
		# make sure there's a query object created
		$self->ensure_query();
		
		# register the namespace
		$self->register_variable_namespace($$self{'Name'},
			[$$self{'Query'}->get_results(), cppvar('bool IsAuthenticated'), cppvar('std::string Credentials')]);
	}
	elsif($phase == WebAppFramework::Unit::PHASE_GLOBAL_H_DECLARATION)
	{
		# write the query object
		$output->write_code($$self{'Query'}->generate_h());
		
		# extras...
		my ($constructor_extra_args, $extra_vars);
		if($$self{'TokenFilter'} eq 'MD5')
		{
			$constructor_extra_args = ', WebApplication &rApplication';
			$extra_vars = "\n\t\t\t\tstatic std::string msTokenFilterSecret;\n\t\t\t\tvoid FilterToken(const std::string &rIn, std::string &rOut);";			
		}
		
		# write the derived class
		my $classname = $$self{'Name'}.'Impl';
		my $basename = $$self{'Query'}->get_name();
		$output->write_code(<<__E);
			class WebApplication;	// for basic access
			class ${classname} : public ${basename}
			{
			public:
				${classname}(DatabaseConnection &rConnection$constructor_extra_args);
				~${classname}();
			private:
				// no copying
				${classname}(const ${classname} &);
				${classname} operator=(const ${classname} &);
			public:
				// hide base class functions
				void Execute();
				
				// Implement extra functions
				std::string MakeCredentials(const std::string &ID, const std::string &Password);
				void Authenticate(const std::string &rCredentials);

				// Find state
				bool IsAuthenticated() const {return mCredentials.size() > 0;}
				const std::string &GetCredentials() const {return mCredentials;}
				// For interfacing to namespace, so can use page var Name.IsAuthenticated
				bool GetIsAuthenticated() const {return IsAuthenticated();}
				
				// Unset authentication state
				void SetToUnauthenticated();
			
			private:
				std::string mCredentials;$extra_vars
			};
__E
	}
	elsif($phase == WebAppFramework::Unit::PHASE_GLOBAL_CPP_CODE)
	{
		# write the query object
		$output->write_code($$self{'Query'}->generate_cpp());
		
		# write the derviced class
		my $tokencolumn = $$self{'TokenColumn'};
		my $classname = $$self{'Name'}.'Impl';
		my $basename = $$self{'Query'}->get_name();
		# get parameters (arguments of Execute function)
		my @args = $$self{'Query'}->get_arguments();
		# does execute need an addition argument?
		my $execute_extra = '';
		if($#args == 1)
		{
			$execute_extra = ', token';
		}
		elsif($#args > 1)
		{
			die "Query for Database::Authenticate has too many paramters";
		}

		# extras...
		my ($constructor_extra_args, $constructor);
		if($$self{'TokenFilter'} eq 'MD5')
		{
			$constructor_extra_args = ', WebApplication &rApplication';
			my $config_var = $$self{'MD5SecretConfigVar'};
			$constructor = <<__E;
				if(msTokenFilterSecret.empty())
				{
					// Load secret from the configuration file -- should only be done once
					const Configuration &rconfig(rApplication.GetConfiguration());
					msTokenFilterSecret = rconfig.GetKeyValue("$config_var");
				}

__E
		}
		
		# write the actual implementations
		$output->write_code(<<__E);
			${classname}::${classname}(DatabaseConnection &rConnection$constructor_extra_args)
				: ${basename}(rConnection)
			{
			$constructor}
			${classname}::~${classname}()
			{
			}
			void ${classname}::Authenticate(const std::string &rCredentials)
			{
				// Un-authenticate
				mCredentials.erase();
			
				// Split into id and token
				std::string::size_type separator = rCredentials.find('~');
				if(separator != std::string::npos)
				{
					// Split into the two strings
					std::string id(rCredentials.substr(0, separator));
					std::string token(rCredentials.substr(separator + 1));
				
					// Run the query
					${basename}::Execute(id$execute_extra);
				
					// Got a result (and one only?)
					if(GetNumberRows() == 1 && Next())
					{
						// Check against token
__E
		if($$self{'TokenFilter'} eq 'MD5')
		{
			$output->write_code(<<__E);
						std::string generatedToken;
						FilterToken(Get$tokencolumn(), generatedToken);
						if(generatedToken == token)
__E
		}
		else
		{
			$output->write_code(<<__E);
						if(Get$tokencolumn() == token)
__E
		}
		$output->write_code(<<__E);
						{
							// Looks good!
							mCredentials = rCredentials;
						}
					}
				}		
			}
			void ${classname}::SetToUnauthenticated()
			{
				if(IsAuthenticated())
				{
					// There is some data ready for retrieval, get rid of it
					// by moving onto the next row, which shouldn't exist because
					// we only allow things to be authenticated if there is 1 row.
					Next();
				}
				
				// Unset the credentials, to mark as not authenticated
				mCredentials.erase();
			}
__E

		if($$self{'TokenFilter'} eq 'MD5')
		{
			# write the filter code function
			$output->write_code(<<__E);
			std::string ${classname}::MakeCredentials(const std::string &ID, const std::string &Password)
			{
				std::string t;
				FilterToken(Password, t);
				return ID + '~' + t;
			}
			void ${classname}::FilterToken(const std::string &rIn, std::string &rOut)
			{
				MD5Digest digest;
				digest.Add(msTokenFilterSecret);
				digest.Add(rIn);
				digest.Add(msTokenFilterSecret);
				digest.Finish();
				rOut = digest.DigestAsString();
			}
			std::string ${classname}::msTokenFilterSecret;
__E
		}
		else
		{
			$output->write_code(<<__E);
			std::string ${classname}::MakeCredentials(const std::string &ID, const std::string &Password)
			{
				return ID + '~' + Password;
			}
__E
		}
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS)
	{
		my $extra = ($$self{'TokenFilter'} eq 'MD5')?', *this':'';
		$output->write_code($$self{'Name'}.'Impl '.$$self{'Name'}."(mApplication.GetDatabaseConnection()$extra);\n");
	}
	elsif($phase == WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_SECURITY)
	{
		# Get credentials, try to authenticate
		$output->write_code($$self{'Name'}.'.Authenticate('.
			($self->get_variable($$self{'CredentialsSource'})->convert_to('std::string'))
			.");\n");
		# redirect?
		my @disable_on_pages;
		if(exists $$self{'DisableRedirectOnPages'})
		{
			@disable_on_pages = $self->list_to_array($$self{'DisableRedirectOnPages'});
		}
		my $disable = 0;
		for(@disable_on_pages) {$disable = 1 if $_ eq $self->get_pagename()}
		if(exists $$self{'RedirectToOnAuthFailure'} && !$disable)
		{
			$output->write_code('if(!'.$$self{'Name'}.".IsAuthenticated())\n{\n");
			$output->write_code($self->make_redirect_code(@{$$self{'RedirectToOnAuthFailure'}}));
			# will output a return statement
			$output->write_code("\n}\n");
		}
	}
}

sub ensure_query()
{
	my ($self) = @_;
	if(ref($$self{'Query'}) ne 'Database::Query')
	{
		# create a database query object as the given data isn't such an object
		my %p = %{$$self{'Query'}};
		$p{'Name'} = $$self{'Name'}.'Base' unless exists $p{'Name'};
		my $q = Database::Query->new(%p);
		$$self{'Query'} = $q;
	}
}

# write the code for the validate function
sub generate_validate_code
{
	my ($self, $id_field, $token_field) = @_;

	# make sure query is set up correctly
	$self->ensure_query();

	# arguments for the Execute query built
	my @args = $$self{'Query'}->get_arguments();
	my $execute_args = 'm'.$id_field;
	$execute_args .= ', m'.$token_field if $#args > 0;
	
	# get other data
	my $token_column = $$self{'TokenColumn'};
	my $auth_name = $$self{'Name'};

	# return generated code
	return <<__E;
		${auth_name}Base query(rApplication.GetDatabaseConnection());
		query.Execute($execute_args);
		if(query.Next())
		{
			if(query.Get${token_column}() == m$token_field)
			{
				m${token_field}ValidityError = WebAppForm::Valid;
			}
		}
__E
}

# set up a form to validate the password
sub set_validate_function_on_form
{
	my ($self, $webapp, $form, $id_field, $token_field) = @_;
	
	# check a few things on the form
	my $form_args_to_validate = ($form->param_exists('ArgsToValidate'))?($form->get('ArgsToValidate')):'';
	die "In Database::Authenticate, set_validate_function_on_form, form doesn't have parameter 'ArgsToValidate' => 'Application'"
		unless $form_args_to_validate eq 'Application';
	my $form_validation = ($form->param_exists('FormValidation'))?($form->get('FormValidation')):'';
	die "In Database::Authenticate, set_validate_function_on_form, form doesn't have parameter 'FormValidation' => 'simple'"
		unless $form_validation eq 'simple';

	# collect extra names for making the validate function
	my $wan = $webapp->get_webapp_name();
	my $dcn = $wan.'Form'.ucfirst($form->get('FormName'));
	
	# Now write a suitable function
	my $fn = <<__E;
	void ${dcn}::Validate($wan &rApplication)
	{
		// Autogenerated by Database::Authenticate unit
__E
	$fn .= $self->generate_validate_code($id_field, $token_field);
	$fn .= "\t}\n";
	
	# add it to the tree
	$form->add_post_unit(WebAppFramework::Unit::Code->new('Phase' => 'main_code', 'Code' => $fn));
}


sub set_HandleSubmission_on_form
{
	my ($self, $form, $id_field, $token_field, @link_spec) = @_;

	# data required
	my $formname = $form->get('FormName');
	my $auth_name = $$self{'Name'};

	# how should the credentials be set?
	my $credentialsSource = $$self{'CredentialsSource'};
	my $set_credentials_line;
	if($credentialsSource =~ m/\Acookie\.(.+?)\Z/)
	{
		# simply set the cookie!
		$set_credentials_line = qq!rResponse.SetCookie("$1", credentials.c_str());\n!;
	}
	elsif($credentialsSource =~ m/\Aparams\.(.+?)\Z/)
	{
		# slightly more difficult here, need to modify the link
		my @addition = ($1,'LOCAL:std::string credentials');
		# is it a normal list, or an anon array?
		if(ref($link_spec[0]))
		{
			push @{$link_spec[0]},@addition
		}
		else
		{
			push @link_spec,@addition
		}
	}
	else
	{
		die "Database::Authenticate unit can't generate HandleSubmission code for CredentialsSource $credentialsSource"
	}

	# build link specification
	my $link_spec_text = $self->link_spec_to_WAF_code(@link_spec);
	
	# write the code
	$form->set('HandleSubmission' => <<__E);
		// Set the credentials
		std::string credentials(${auth_name}.MakeCredentials(${formname}.Get$id_field(), ${formname}.Get$token_field()));
		$set_credentials_line
		// Set redirect
		std::string uri($link_spec_text);
		rResponse.SetAsRedirect(uri.c_str());

		return true;
__E
}



sub get_required_headers_this
{
	my ($self, $type) = @_;
	if($type == WebAppFramework::Unit::HEADERS_GLOBAL_H_PROJECT)
	{
		if($$self{'TokenFilter'} eq 'MD5')
		{
			return ('DatabaseQuery.h', 'MD5Digest.h', 'Configuration.h', 'WebApplication.h')
		}
		else
		{
			return ('DatabaseQuery.h')
		}
	}
	return ()
}

sub in_default_page()
{
	my ($self) = @_;
	
	$$self{'_in_default_page'} = 1;
}


1;

