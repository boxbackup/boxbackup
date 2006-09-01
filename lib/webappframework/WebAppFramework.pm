package WebAppFramework;
use strict;
use Symbol;
use vars qw/%_file_ext_to_mime_type/;

use constant LANG_NAME => 0;

use constant PAGE_SHORT_NAME => 0;
use constant PAGE_PARAMS_START => 1;

use constant URL_ELEMENTS_PARAMS_START => 3;

# include modules
use WebAppFramework::Unit;
use WebAppFramework::Output;
use CppVariable;
use CppDataClass;

# define MIME types
%_file_ext_to_mime_type = (
	'html' => 'text/html; charset=UTF-8',
	'txt' => 'text/plain',
	'css' => 'text/css',
	'jpg' => 'image/jpeg',
	'jpeg' => 'image/jpeg',
	'gif' => 'image/gif',
	'png' => 'image/png',
	'ico' => 'image/vnd.microsoft.icon'
);

sub new
{
	my $self = {};
	$$self{'languages'} = [];
	$$self{'languages_locale_exception'} = {};
	$$self{'default_language'} = '';
	$$self{'global_params'} = [];
	$$self{'pages'} = {};
	$$self{'webappname'} = 'generic-webapp';
	$$self{'current_language'} = 'NOT-SET';
	$$self{'static_dirs'} = [];
	bless $self;
	$self
}

sub set_webapp_name
{
	my ($self, $name, $daemon_name, $url_base) = @_;
	
	$$self{'webappname'} = $name;
	$$self{'daemon_name'} = $daemon_name;
	$$self{'url_base'} = $url_base;
}

sub get_webapp_name
{
	my ($self) = @_;
	
	return $$self{'webappname'}
}

sub add_language
{
	my ($self, $language_name, $locale_obj) = @_;
	
	die "Language name '$language_name' is too long (4 chars max)"
		unless length($language_name) <= 4;
	
	push @{$$self{'languages'}},[$language_name];
	
	# if a locale is specified, store it
	if(defined $locale_obj)
	{
		${$$self{'languages_locale_exception'}}{$language_name} = $locale_obj
	}
}

sub set_default_langage
{
	my ($self, $default_language) = @_;
	
	$$self{'default_language'} = $default_language;
}

sub set_current_langage
{
	my ($self, $language) = @_;
	
	$$self{'current_language'} = $language;
}

sub add_global_parameters
{
	my ($self, @params) = @_;

	push @{$$self{'global_params'}}, CppVariable::var_list(@params)
}

sub get_global_parameters
{
	my ($self, @params) = @_;

	return @{$$self{'global_params'}};
}

sub get_page_parameters
{
	my ($self, $pagename) = @_;
	
	die "Page $pagename not known"
		unless exists ${$$self{'pages'}}{$pagename};
	
	my $pi = ${$$self{'pages'}}{$pagename};
	# return global parameters, followed by this page's parameteres
	return (@{$$self{'global_params'}}, @{$pi}[PAGE_PARAMS_START .. $#$pi]);
}

sub add_page
{
	my ($self, $name, $short_name, @params) = @_;
	
	die "Short name '$short_name' for page '$name' is not exactly four characters long"
		unless length($short_name) == 4;
	
	${$$self{'pages'}}{$name} = [$short_name, CppVariable::var_list(@params)];
}

sub add_extra_config_directive
{
	my ($self, $type, $name) = @_;

	$$self{'extra_config_directives'} = [] unless exists $$self{'extra_config_directives'};
	push @{$$self{'extra_config_directives'}},[$type,$name]
}

# arg 1 is redirect/rewrite, arg 2 is URL base
sub set_homepage
{
	my ($self, $type, $uri) = @_;
	print "homepage type must be 'redirect' or 'rewrite'" unless $type eq 'redirect' || $type eq 'rewrite';
	$$self{'homepage'} = [$type, $uri];
}

sub initialise_page
{
	my ($self, $page, $pagename) = @_;
	
	# find the page
	die "Can't find page $pagename in list of known pages" unless exists ${$$self{'pages'}}{$pagename};
	my $pi = ${$$self{'pages'}}{$pagename};

	# tell page about the web application
	$page->set_webapp($self);
	
	# and it's name
	$page->set_pagename($pagename);

	# set global parameters
	$page->add_parameters(@{$$self{'global_params'}});
	# and the page local ones
	$page->add_parameters(@{$pi}[PAGE_PARAMS_START .. $#{$pi}]);
}

sub write_makefile
{
	my ($self, $makefile_f, $app_description) = @_;
	
	my $webappname = $$self{'webappname'};
	
	# build a list of HTML files for the dependencies
	opendir DIR, "Templates";
	my $html_files = join(' ',map {'Templates/'.$_} grep(/\.html\Z/, readdir(DIR)));
	closedir DIR;

	# build a list of language files for the dependencies
	my @langfiles = ('default');
	for my $lang (@{$$self{'languages'}})
	{
		push @langfiles,$$lang[LANG_NAME]
			unless $$lang[LANG_NAME] eq $$self{'default_language'}
	}
	my $language_files = join(' ',map {'Languages/'.$_.'.txt'} @langfiles);

	while(my ($nm,$pi) = each %{$$self{'pages'}})
	{
		# test for existance
		if(!-f "Pages/$nm.pl")
		{
			die "Page definition script Pages/$nm.pl does not exist";
		}
		
		# dependencies for the page
		my $deps = "Pages/$nm.pl $app_description ../../lib/webappframework/WebAppFramework.pm";
		# command to run to generate these pages (base)
		my $page_cmd = "perl ../../lib/webappframework/WebApplication.pl $app_description page $nm";
		
		# write makefile entries
		# 1. The common code and header file
		print $makefile_f "autogen_webapp/${webappname}Page$nm.cpp autogen_webapp/${webappname}Page$nm.h: $deps\n\t$page_cmd\n\n";

		# run the script to generate the first files
		die "Could not generate page $nm" unless system($page_cmd) == 0;

		# 2. For all the languages		
		for my $lang (@{$$self{'languages'}})
		{
			my $l = $$lang[LANG_NAME];
			print $makefile_f "autogen_webapp/${webappname}Page${nm}_$l.cpp: $deps $html_files\n\t$page_cmd $l\n\n";
			# run script
			die "Could not generate page $nm, language $l" unless system("$page_cmd $l") == 0;
		}
	}
	
	# write line for the main application class
	print $makefile_f "autogen_webapp/${webappname}Server.cpp autogen_webapp/${webappname}Server.h autogen_webapp/${webappname}Global.cpp autogen_webapp/${webappname}Global.h $language_files: $app_description ../../lib/webappframework/WebAppFramework.pm\n";
	print $makefile_f "\tperl ../../lib/webappframework/WebApplication.pl $app_description make\n\n";
}


sub write_global_code()
{
	my ($self, $page) = @_;

	my $webappname = $$self{'webappname'};

	# Make header file
	my $global_h = gensym;
	open $global_h, ">autogen_webapp/${webappname}Global.h" or die "Can't open global h file for writing";
	
	write_h_boilerplate_begin($global_h, "${webappname}Global");
	print $global_h $_ for ((map {"#include <$_>\n"} ('vector','string',$page->get_required_headers(WebAppFramework::Unit::HEADERS_GLOBAL_H_SYSTEM))),
		(map {qq`#include "$_"\n`} $page->get_required_headers(WebAppFramework::Unit::HEADERS_GLOBAL_H_PROJECT)));

	# write global .h file
	{
		my $output = WebAppFramework::Output->new($global_h, '_APP_GLOBAL');
		$output->setup_languages('COMMON', $$self{'default_language'}, '_APP_GLOBAL');
		$output->set_code_preprocessor($page);

		# run through all the phases, writing output from all the units
		for my $phase (WebAppFramework::Unit::PHASE_INITIALISE,
			WebAppFramework::Unit::PHASE_GLOBAL_H__BEGIN .. WebAppFramework::Unit::PHASE_GLOBAL_H__END)
		{
			# get the page to write itself
			$page->write($output, $phase, 0);
		}

		# write updated language files
		$output->save_language_files();
	}

	# finish the .h file
	write_h_boilerplate_end($global_h, "${webappname}Global");
	close $global_h;

	# make cpp file
	my $global_cpp = gensym;
	open $global_cpp, ">autogen_webapp/${webappname}Global.cpp" or die "Can't open global cpp file for writing";

	write_cpp_boilerplate_begin($global_cpp, "${webappname}Global");
	# write out the various headers
	print $global_cpp qq!#include "${webappname}Global.h"\n\n#include "MemLeakFindOn.h"\n\n!;
	
	# Write the code for the cpp file
	{
		my $output = WebAppFramework::Output->new($global_cpp, '_APP_GLOBAL');
		$output->setup_languages('COMMON', $$self{'default_language'}, '_APP_GLOBAL');
		$output->set_code_preprocessor($page);

		# run through all the phases, writing output from all the units
		for my $phase (WebAppFramework::Unit::PHASE_INITIALISE,
			WebAppFramework::Unit::PHASE_GLOBAL_CPP__BEGIN .. WebAppFramework::Unit::PHASE_GLOBAL_CPP__END)
		{
			# get the page to write itself
			$page->write($output, $phase, 0);
		}

		# write updated language files
		$output->save_language_files();
	}
	
	close $global_cpp;
}


sub write_server_class
{
	my ($self) = @_;
	
	my $h = gensym;
	my $cpp = gensym;
	
	my $classname = $$self{'webappname'}.'Server';
	my $appclassname = $$self{'webappname'};
	my $daemon_name = $$self{'daemon_name'};
	my $url_base = $$self{'url_base'};

	open $h,">autogen_webapp/$classname.h" or die "Can't open server h file";
	open $cpp,">autogen_webapp/$classname.cpp" or die "Can't open server h file";
	
	# write header file
	write_h_boilerplate_begin($h, $classname);
	
	print $h <<__E;
#include <string>
#include <vector>

#include "WebApplication.h"
#include "../$appclassname.h"

class $classname : public WebApplication
{
public:
	$classname();
	~$classname();
private:
	// no copying
	$classname(const $classname &);
	$classname &operator=(const $classname &);
public:

	const char *DaemonName() const;

	$appclassname &GetApplication() {return mApplication;}
	virtual WebApplicationObject &GetApplicationObject();
	virtual const char *GetURLBase() const;

	bool HandlePage(const HTTPRequest &rRequest, HTTPResponse &rResponse,
		uint32_t Language, uint32_t Page, std::vector<std::string> &rURLElements);
	bool GetStaticFile(const char *URI, const void **ppFileOut,
		int *pFileSizeOut, const char **ppFileMIMETypeOut);
__E
	if(exists $$self{'homepage'})
	{
		# add in function for homepage info
		print $h <<__E;
	bool GetHomePageURI(std::string &rHomePageLocation, bool &AsRedirect) const;
__E
	}

	# add in handle functions for each of the pages for each of the languages
	while(my ($nm,$pi) = each %{$$self{'pages'}})
	{
		for my $lang (@{$$self{'languages'}})
		{
			my $l = $$lang[LANG_NAME];
			print $h "\tbool HandlePage${nm}_$l(const HTTPRequest &rRequest, HTTPResponse &rResponse, const std::vector<std::string> &rElements);\n";
		}
	}

	if(exists $$self{'extra_config_directives'})
	{
		print $h <<__E;

protected:
	const ConfigurationVerify *GetConfigVerify() const;
__E
	}

	print $h <<__E;

private:
	$appclassname mApplication;
};

__E
	
	write_h_boilerplate_end($h, $classname);

	# write CPP file
	write_cpp_boilerplate_begin($cpp, $classname);
	print $cpp <<__E;
#include <string.h>

#include "$classname.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"

#include "MemLeakFindOn.h"


${classname}::$classname()
{
}
${classname}::~$classname()
{
}
const char *${classname}::DaemonName() const
{
	return "$daemon_name";
}
bool ${classname}::HandlePage(const HTTPRequest &rRequest, HTTPResponse &rResponse,
		uint32_t Language, uint32_t Page, std::vector<std::string> &rURLElements)
{
	// Switch on language and page name to dispatch to handling function
	switch(Language)
	{
__E
	for my $lang_en (@{$$self{'languages'}})
	{
		my $l = $$lang_en[LANG_NAME];
		my $l_v = element_to_string($l);
		print $cpp <<__E;
	case $l_v: // $l
		{
			switch(Page)
			{
__E
		while(my ($nm,$pi) = each %{$$self{'pages'}})
		{
			my $ns = $$pi[PAGE_SHORT_NAME];
			my $n_v = element_to_string($ns);

			print $cpp <<__E;
			case $n_v: // $ns ($nm)
				return HandlePage${nm}_$l(rRequest, rResponse, rURLElements);
				break;
__E
		}
		print $cpp <<__E;
			default:
				break;
			}
		}
		break;
__E
	}

	print $cpp <<__E;
	default:
		break;
	}
	// not handled by default
	return false;
}
const char *${classname}::GetURLBase() const
{
	return "$url_base";
}
WebApplicationObject &${classname}::GetApplicationObject()
{
	return mApplication;
}
__E
	if(exists $$self{'extra_config_directives'})
	{
		# write a config directive function
		print $cpp <<__E;		
const ConfigurationVerify *${classname}::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		HTTPSERVER_VERIFY_SERVER_KEYS(0)	// no default addresses
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server",
			0,
			verifyserverkeys,
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};
	
	static ConfigurationVerifyKey verifyrootkeys[] = 
	{
__E
		for(@{$$self{'extra_config_directives'}})
		{
			my ($type,$name) = @$_;
			my $e = ($type eq 'int')?'ConfigTest_IsInt | ':'';
			print $cpp qq!\t\t{"$name", 0, ${e}ConfigTest_Exists, 0},\n!;
		}

		print $cpp <<__E;		
		HTTPSERVER_VERIFY_ROOT_KEYS
	};

	static ConfigurationVerify verify =
	{
		"root",
		verifyserver,
		verifyrootkeys,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}
__E
	}
	
	if(exists $$self{'homepage'})
	{
		# add in function for homepage info
		my ($type,$uri) = @{$$self{'homepage'}};
		my $rdr = ($type eq 'redirect')?'true':'false';
		print $cpp <<__E;
bool ${classname}::GetHomePageURI(std::string &rHomePageLocation, bool &rAsRedirect) const
{
	rHomePageLocation = "$uri";
	rAsRedirect = $rdr;
	return true;
}
__E
	}
	# write any static files
	$self->write_static_file_function($cpp);

	close $cpp;
	close $h;
}

sub write_page
{
	my ($self, $page, $page_name, $language) = @_;
	
	my $webappname = $$self{'webappname'};

	# create headers in string...
	my $include_headers = '';
	# collect together system includes
	my @system_headers = $page->get_required_headers(WebAppFramework::Unit::HEADERS_SYSTEM);
	$include_headers .= "#include <$_>\n" for(@system_headers);
	# include the header file for the main server class, the .h file for this page, and the various
	# standard header files which will be required.
	$include_headers .= <<__E;
#include "${webappname}Server.h"
#include "${webappname}Global.h"
#include "${webappname}Page$page_name.h"
#include "HTTPResponse.h"
#include "HTTPRequest.h"
__E
	# collect together project includes
	my @project_headers = $page->get_required_headers(WebAppFramework::Unit::HEADERS_PROJECT);
	$include_headers .= "#include \"$_\"\n" for(@project_headers, 'Conversion.h', 'autogen_ConversionException.h');

	# class which contains all the parameters
	my $param_classname = "${webappname}Page${page_name}Parameters";

	if($language eq 'COMMON')
	{
		# Write all the common code
		
		# first, generate a data class for the parameters
		my $param_class = CppDataClass->new($param_classname, '', $page->get_parameters());
		$param_class->set_option('ReadOnly', 1);
		$param_class->add_declarations(CppDataClass::PUBLIC, 'bool SetFromURLElements(const std::vector<std::string> &rElements);');

		# Make header file
		my $main_h = gensym;
		open $main_h, ">autogen_webapp/${webappname}Page$page_name.h" or die "Can't open main h file for $page_name";
		
		write_h_boilerplate_begin($main_h, "${webappname}Page$page_name");
		print $main_h $_ for ((map {"#include <$_>\n"} ('vector','string',$page->get_required_headers(WebAppFramework::Unit::HEADERS_PAGE_H_SYSTEM))),
			(map {qq`#include "$_"\n`} $page->get_required_headers(WebAppFramework::Unit::HEADERS_PAGE_H_PROJECT)));
		print $main_h "class HTTPRequest;\nclass HTTPResponse;\n\n";
		
		{
			# create an output object
			my $output = WebAppFramework::Output->new($main_h, $page_name.'_COMMON');
			$output->setup_languages('COMMON', $$self{'default_language'}, $page_name);
			$output->set_code_preprocessor($page);

			# write the list of defines for translated strings
			$page->write_translated_strings($output, 1);	# 1 means "write defines"
			
			# write parameter class
			$output->write_code($param_class->generate_h());
			
			# run through all the phases, writing output from all the units
			for my $phase (WebAppFramework::Unit::PHASE_INITIALISE,
				WebAppFramework::Unit::PHASE_MAIN_H__BEGIN .. WebAppFramework::Unit::PHASE_MAIN_H__END)
			{
				# get the page to write itself
				$page->write($output, $phase, 0);
			}

			# write updated language files
			$output->save_language_files();
		}
		
		# finish the .h file
		write_h_boilerplate_end($main_h, "${webappname}Page$page_name");
		close $main_h;

		# make cpp file
		my $main_cpp = gensym;
		open $main_cpp, ">autogen_webapp/${webappname}Page$page_name.cpp" or die "Can't open main cpp file for $page_name";

		write_cpp_boilerplate_begin($main_cpp, "${webappname}Page$page_name");
		# write out the various headers
		print $main_cpp $include_headers;
		print $main_cpp qq`\n#include "MemLeakFindOn.h"\n\n`;

		# get units to write code
		{
			# create an output object
			my $output = WebAppFramework::Output->new($main_cpp, $page_name.'_COMMON');
			$output->setup_languages('COMMON', $$self{'default_language'}, $page_name);
			$output->set_code_preprocessor($page);

			# write parameter class
			$output->write_code($param_class->generate_cpp());
			
			# write the function to set the parameters from the URL elements
			my @params = $page->get_parameters();
			my $element_n = URL_ELEMENTS_PARAMS_START;	# first element index to consider
			my $required_elements_size = URL_ELEMENTS_PARAMS_START + $#params + 1;
			$output->write_code(<<__E);
bool ${param_classname}::SetFromURLElements(const std::vector<std::string> &rElements)
{
	if(rElements.size() != $required_elements_size)
	{
		return false;
	}
	try
	{
__E
			# write code for each of the parameters
			for my $p (@params)
			{
				# and and converted value
				my $name = $p->name();
				my $converted = $p->convert_from('std::string', "rElements[$element_n]");
				
				# write code
				$output->write_code(<<__E);
		m$name = $converted;
__E
				$element_n++;
			}

			$output->write_code(<<__E);
	}
	catch(ConversionException &e)
	{
		// If there was a problem converting any of the values, an exception will be thrown.
		// Convert to simple "not valid" error return
		return false;
	}
	
	return true;
}
__E
			# run through all the phases, writing output
			for my $phase (WebAppFramework::Unit::PHASE_INITIALISE,
				WebAppFramework::Unit::PHASE_MAIN_CPP__BEGIN .. WebAppFramework::Unit::PHASE_MAIN_CPP__END)
			{
				# get the page to write itself
				$page->write($output, $phase, 0);
			}

			# write updated language files
			$output->save_language_files();
		}

		close $main_cpp;
	}
	else
	{
		# get a locale object, either supplied by the author or automatically
		my $locale;
		if(exists ${$$self{'languages_locale_exception'}}{$language})
		{
			# use supplied locale
			$locale = ${$$self{'languages_locale_exception'}}{$language}
		}
		else
		{
			# load the default locale object for this language
			my $objtype = 'WebAppFramework::Locale::'.$language;
			eval <<__E;
				use $objtype;
				\$locale = $objtype->new();
__E
			if($@)
			{
				die "Failed to automatically generate locale object for language $language\n(type is $objtype)\n";
			}
		}
		# and tell the page about it.
		$page->set_locale($locale);
	
		# write a language file
		my $cpp = gensym;
		open $cpp, ">autogen_webapp/${webappname}Page${page_name}_$language.cpp" or die "Can't open language $language cpp file for $page_name";
		
		# write the various bits of boilerplate
		write_cpp_boilerplate_begin($cpp, "${webappname}Page${page_name}_$language");
		print $cpp $include_headers;
		print $cpp '#include "'.$locale->get_cpp_include_name()."\"\n";
		print $cpp qq`\n#include "MemLeakFindOn.h"\n\n`;
		
		# dump the map of the page, for debugging
		print $cpp "\n/*\n\n";
		$page->dump_structure($cpp, 'root', 0);
		print $cpp "\n*/\n\n";

		# create an output object
		my $output = WebAppFramework::Output->new($cpp, $page_name.'_'.$language);
		$output->setup_languages($language, $$self{'default_language'}, $page_name);
		$output->set_code_preprocessor($page);
		
		# write the char* list of translated strings
		$page->write_translated_strings($output, 0);	# 1 means "write strings"

		# set up the text which is output at the beginning of each phase
		my @phase_prefix;
		my $locale_cpp_classname = $locale->get_cpp_classname();
		$phase_prefix[WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_VARS] = <<__E;
bool ${webappname}Server::HandlePage${page_name}_$language(const HTTPRequest &rRequest, HTTPResponse &rResponse, const std::vector<std::string> &rElements)
{
	// Locale
	$locale_cpp_classname locale;
	// Decode the parameters
	$param_classname params;
	if(!params.SetFromURLElements(rElements))
	{
		return false;
	}
__E
		$phase_prefix[WebAppFramework::Unit::PHASE_LANG_CPP_HANDLE_OUTPUT] = <<__E;
	rResponse.SetResponseCode(HTTPResponse::Code_OK);
	rResponse.SetContentType("text/html; charset=UTF-8");
__E
		$phase_prefix[WebAppFramework::Unit::PHASE_LANG_CPP_FINISH] = <<__E;
	return true;
}
__E

		# run through all the phases, writing output
		for my $phase (WebAppFramework::Unit::PHASE_INITIALISE,
			WebAppFramework::Unit::PHASE_LANG_CPP__BEGIN .. WebAppFramework::Unit::PHASE_LANG_CPP__END)
		{
			# write any prefix to this phase
			$output->write_code($phase_prefix[$phase]);
			
			# get the page to write itself
			$page->write($output, $phase, 0);
			
			# flush any text to be written
			$output->flush_text();
		}
		
		# write updated language files
		$output->save_language_files();

		close $cpp;
	}
}

sub write_h_boilerplate_begin
{
	my ($f, $filename_base) = @_;
	
	my $hg = 'WEBAPPAUTOGEN_'.uc($filename_base).'__H';
	
	print $f <<__E;
//
// Automatically generated file, do not edit
//
#ifndef $hg
#define $hg

__E
}
sub write_h_boilerplate_end
{
	my ($f, $filename_base) = @_;
	
	my $hg = 'WEBAPPAUTOGEN_'.uc($filename_base).'__H';
	
	print $f <<__E;

#endif // $hg

__E
}

sub write_cpp_boilerplate_begin
{
	my ($f, $filename_base) = @_;
	
	my $hg = 'WEBAPPAUTOGEN_'.uc($filename_base).'__H';
	
	print $f <<__E;
//
// Automatically generated file, do not edit
//
#include "Box.h"

__E
}

sub element_to_string
{
	my ($e) = @_;

	my $s = '0x';
	for(reverse split(//,substr($e, 0, 4)))
	{
		$s .= sprintf("%02x", unpack('C',$_))
	}

	return $s
}

# returns the base URL for a page, including the current language
sub get_url_base_for_page
{
	my ($self, $pagename) = @_;
	
	die "Page $pagename not known" unless exists ${$$self{'pages'}}{$pagename};
	
	my $pi = ${$$self{'pages'}}{$pagename};
	return '/'.$$self{'url_base'}.'/'.$$self{'current_language'}.'/'.$$pi[PAGE_SHORT_NAME]
}

sub get_url_base
{
	my ($self) = @_;
	return '/'.$$self{'url_base'};
}

# static file handling
# Add a mime type
sub add_file_ext_to_mime_type
{
	my ($self, $ext, $mime_type) = @_;
	$_file_ext_to_mime_type{lc($ext)} = $mime_type;
}

# Add a static directory
sub add_static_directory
{
	my ($self, $dirname, $web_dirname) = @_;
	if($dirname =~ m!(\A/|/\Z)!)
	{
		die "Static directory $dirname: URI $web_dirname starts or ends with '/'\n";
	}
	push @{$$self{'static_dirs'}}, [$dirname, $web_dirname];
}

# Write the static files
sub write_static_file_function
{
	my ($self, $cpp) = @_;
	my $webappname = $$self{'webappname'};

	my $code = '';
	my $file_num = 0;

	# then, for each directory write out the static files
	for my $d (@{$$self{'static_dirs'}})
	{
		my ($dirname, $web_dirname) = @$d;
		my $dirbegins = ($web_dirname eq '')?'/':"/$web_dirname/";
		my $dirbegins_len = length($dirbegins);
		$code .= qq!\tif(::strncmp(URI, "$dirbegins", $dirbegins_len) == 0)\n\t{\n!;
		
		# now for each file, write a static variable declaration and an if statement
		opendir DIR,$dirname or die "Can't open dir $dirname";
		my @dir = readdir DIR;
		closedir DIR;
		for my $leaf (@dir)
		{
			next unless $leaf =~ m/\A(.+)\.(.+)\Z/;
			my ($file,$name,$ext) = ("$dirname/$1.$2", "$1.$2", $2);
			my $data_len = -s $file;
			open FILE,$file or die "Can't open $file";
			my $data;
			read FILE,$data,$data_len;
			close FILE;
			
			# declaration
			print $cpp "static const char *StaticFile$file_num = ";
			
			# split up the file (not the best way of doing this, of course...)
			my $first_line = 1;
			for(split /(.{0,80})/s, $data)
			{
				next if $_ eq '';
				
				# line start
				print $cpp (($first_line)?'"':" \\\n\t\t\"");
				$first_line = 0;
		
				# Alter partial line to fit in a nice C string
				# WARNING: Must not change the size of the string by these transforms!
				my $t = $_;
				$t =~ s/\\/\\\\/g;
				$t =~ s/\n/\\n/g;
				$t =~ s/\t/\\t/g;
				$t =~ s/"/\\"/g;
				$t =~ s/([\x00-\x1f\x7d-\xff])/'\x'.sprintf("%02x", ord($1))/eg;
				$t =~ s/(\\x[0-9a-f][0-9a-f])([0-9a-fA-F])/$1.'""'.$2/eg;
		
				# output data
				print $cpp $t,'"';	
			}
			print $cpp ";\n";
			
			# find MIME type
			my $mime_type = $_file_ext_to_mime_type{lc($ext)};
			die "Don't know about MIME type for file extension $ext, use add_file_ext_to_mime_type() to set\n"
				if $mime_type eq '';
			
			# write the code to check for this file
			$code .= <<__E;
		if(::strcmp(URI + $dirbegins_len, "$name") == 0)
		{
			*ppFileOut = StaticFile$file_num;
			*pFileSizeOut = $data_len;
			*ppFileMIMETypeOut = "$mime_type";
			return true;
		}
__E
			
			# increment file number
			$file_num++;
		}
		
		$code .= "\t}\n"; 
	}

	# write the actual function
	print $cpp <<__E;
bool ${webappname}Server::GetStaticFile(const char *URI, const void **ppFileOut, int *pFileSizeOut, const char **ppFileMIMETypeOut)
{
$code

	// static file not found.
	return false;
}
__E
}

1;

