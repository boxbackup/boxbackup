use WebAppFramework::Unit::PageTemplate;
use WebAppFramework::Unit::TableContainer;
use WebAppFramework::Unit::Variable;
use WebAppFramework::Unit::Form;
use WebAppFramework::Unit::Menu;
use WebAppFramework::Unit::Database::Table;
use WebAppFramework::Unit::Database::Authenticate;
use WebAppFramework::Unit::OutputIf;
use WebAppFramework::Locale::en;

$webapp->set_webapp_name('TestWebApp', 'testwebapp', 'test');

# Add some extra parameters in the config file
$webapp->add_extra_config_directive('string', 'DatabaseDriver');
$webapp->add_extra_config_directive('string', 'DatabaseConnection');

# for the secret for MD5 hashing of the user's password...
$webapp->add_extra_config_directive('string', 'CredentialTokenSecret');


$webapp->add_language('en');
$webapp->add_language('CAPS', WebAppFramework::Locale::en->new());
$webapp->set_default_langage('en');

# set up global parameters for all pages
$webapp->add_global_parameters(cppvar('std::string','Username'),'int32_t UserLoginToken 0');
# $webapp->add_global_parameters(cppvar('std::string','Credentials',''));

# define each page, and the additional parameters it has
$webapp->add_page('Login', 'lgin');
$webapp->add_page('Main', 'main', 'int32_t Value', 'std::string Text', 'int32_t FormShow 0');
$webapp->add_page('NewEntry', 'newe', 'int32_t EditEntryID 0');
$webapp->add_page('DisplayEntry', 'disp', 'int32_t EntryID');
$webapp->add_page('DeleteEntry', 'dele', 'int32_t EntryID');
$webapp->add_page('ListEntries', 'list');
$webapp->add_page('Logout', 'lgou');

# set defaults for various Units
WebAppFramework::Unit::TableContainer->set_defaults('Template' => 'TestWebAppMain');
WebAppFramework::Unit::PageTemplate->set_defaults('Template' => 'TestWebAppMain');
WebAppFramework::Unit::Form->set_defaults('Template' => 'TestWebAppMain', 'FragmentsName' => 'Form');
WebAppFramework::Unit::FormTableContainer->set_defaults('Template' => 'TestWebAppMain', 'FragmentsName' => 'Form');
WebAppFramework::Unit::Database::Table->set_defaults('Template' => 'TestWebAppMain', 'FragmentsName' => 'DatabaseTable');

# Subroutine to set up the basics of the page
sub setup_page
{
	my $page = WebAppFramework::Unit::PageTemplate->new(); # name of template set in defaults
	
	# security
	my $security = WebAppFramework::Unit::Database::Authenticate->new('Name' => 'Security',
		'Query' => {'Statement' => 'SELECT fID,fUsername,fPassword FROM tUsers WHERE fUsername = $1', # AND fPassword = $2',
					'Parameters' => 'std::string Username', #, std::string Password',
					'Results' => 'int ID, std::string Username, std::string Password'},
		'TokenColumn' => 'Password',
		'CredentialsSource' =>  'cookie.Credentials', #'params.Credentials',
		'TokenFilter' => 'MD5',
		'MD5SecretConfigVar' => 'CredentialTokenSecret',
		'RedirectToOnAuthFailure' => ['Login'],
		'DisableRedirectOnPages' => 'Login');
	
	$page->add_pre_unit($security);
	
	# default page contents
	my $menu = WebAppFramework::Unit::Menu->new('Template' => 'TestWebAppMain', 'FragmentsName' => 'Menu',
		Items => [
				['Login', ['Login']],
				['Main page', ['Main', 'Value' => 'CONSTANT:99', 'Text' => 'CONSTANT:SomeText']],
				['New entry', ['NewEntry']],
				['List entries', ['ListEntries']],
				['Logout', ['Logout']]
			]);
	$page->add_unit('MENU', $menu);
	
	# status box
	my $status = WebAppFramework::Unit::TableContainer->new('FragmentsName' => 'Table');
	$status->add_text('0_0', 'Username (param)');
	$status->add_unit('1_0', WebAppFramework::Unit::Variable->new('Variable' => 'params.Username'));
	$status->add_text('0_1', 'Token');
	$status->add_unit('1_1', WebAppFramework::Unit::Variable->new('Variable' => 'params.UserLoginToken'));
	$status->add_text('0_2', 'formdata.Something')
		->link_to('Main', 'Text' => 'params.Username', 'Value' => 'CONSTANT:2')	# link to another page
		->set('class' => 'linkstyle');	# set another attributes in the link
	$status->add_unit('1_2', WebAppFramework::Unit::Variable->new('Variable' => 'formdata.Something'));
	$status->add_text('0_3', 'Username (login)');
	$status->add_unit('1_3', WebAppFramework::Unit::OutputIf->new('Condition' => "WAF::Var('Security.IsAuthenticated')",
									'@true' => WebAppFramework::Unit::Variable->new('Variable' => 'Security.Username'),
									'@false' => WebAppFramework::Unit::TranslatedText->new('Text' => '(no user logged in)')));
	$status->add_text('0_4', 'User is authenticated?');
	$status->add_unit('1_4', WebAppFramework::Unit::Variable->new('Variable' => 'Security.IsAuthenticated'));
	$status->add_text('0_5', 'User Credentials');
	$status->add_unit('1_5', WebAppFramework::Unit::Variable->new('Variable' => 'Security.Credentials'));
	
	$page->add_unit('STATUS', $status);
	
	return $page
}

1;

