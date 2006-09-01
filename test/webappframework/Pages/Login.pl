use WebAppFramework::Unit::Form;
use WebAppFramework::Unit::Variable;
use WebAppFramework::Unit::FormErrorDisplay;
use WebAppFramework::Unit::Code;
use WebAppFramework::Unit::SimpleContainer;
use WebAppFramework::Unit::Database::Table;
use Database::Query;

$page->add_text('TITLE', 'Login');

# Container for everything on the page
my $container = WebAppFramework::Unit::SimpleContainer->new('Separator' => '<br>&nbsp<br>');
# place the container on the page
$page->add_unit('PAGE', $container);

my $form = WebAppFramework::Unit::Form->new('FormName' => 'login', 'FormValidation' => 'simple',
	'ArgsToValidate' => 'Application'); #, 'ExternalErrors' => 1);
	# NOTE: Validation is performed in TestWebAppFormLogin::Validate(),
$form_container = $form->make_container();
$form_container->add_text_field('Username', 'Username', 'length(4,34)', 'Default' => 'params.Username',
			'Size' => 26, 'MaxLength' => 30, 'Attributes' => 'class="css_style"');
$form_container->add_text_field('Password', 'Password', 'external', 'Size' => 20, 'DisplayAsPassword' => 1,
	'ValidationFailMsg' => 'Password incorrect');
$form_container->add_submit_button('DefaultButton', 'Login');

# find the Authenticate unit
my $auth = $page->find_unit('Authenticate', 'Name' => 'Security');

# Use it to generate a validation function for this form.
# This will set the credentials, and redirect to the given page.
$auth->set_validate_function_on_form($webapp, $form, 'Username', 'Password');

# And to set the credentials when the form is submitted
$auth->set_HandleSubmission_on_form($form, 'Username', 'Password',
	['Main', 'Username' => 'login.Username', 'Value' => 'CONSTANT:1', 'Text' => 'CONSTANT:start']);


# move the error display to after the form, just to demonstrate it's possible
#$form->add_post_unit(WebAppFramework::Unit::FormErrorDisplay->new('Form' => $form));

# insert form into the main page container
$container->add_unit('0', $form);

# --------
# see version 1.7 of this file for alternate code for validation and authenticate form handling
# --------

# --------------------------------------------------------------------------------

# Unit to display all the users
my $users = WebAppFramework::Unit::Database::Table->new(
		'Name' => 'users',
		'Query' => Database::Query->new(
				'Name' => 'ListUsersQuery',
				'Statement' => 'runtime',	# don't need to do things this way, but just for a test...
				'Results' => 'int32_t ID,std::string Username,std::string Password'
			)
	);
$container->add_text('1', 'List of all users on system');
$container->add_unit('2', $users);

# use a runtime query, just to be perverse and test out that particular feature.
$users->set('QueryCode' => <<__E);
		ListUsersQuery users(mApplication.GetDatabaseConnection(), "SELECT fID,fUsername,fPassword FROM tUsers WHERE fUsername <> \$1 ORDER BY fUsername");
		users.Execute("s", "very-secret-user");
__E


1;
