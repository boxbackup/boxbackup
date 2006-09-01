use WebAppFramework::Unit::TableContainer;
use WebAppFramework::Unit::Form;
use WebAppFramework::Unit::Variable;
use WebAppFramework::Unit::SimpleContainer;
use WebAppFramework::Unit::Code;
use WebAppFramework::Unit::ListOfLinks;
use WebAppFramework::Unit::FixedPointNumber;

$page->add_translated_string("Error1", "Error message one");
$page->add_translated_string("Error2", "Error message two");

$page->add_text('TITLE', 'Main page');

# The fragments name could be set in the defaults, but in real use, several table
# template would be included in the template file.
my $table = WebAppFramework::Unit::TableContainer->new('FragmentsName' => 'Table');

$table->add_text('0_0', 'Top left');
$table->add_text('0_2_Highlight', 'Bottom left');
$table->add_text('1_1', 'Centre');
$table->add_text('2_0', 'Top right');
$table->add_text('2_2', 'Bottom right')
	->link_to('Main', 'Text' => 'params.UserLoginToken');

my $form = WebAppFramework::Unit::Form->new('FormName' => 'widgets', 'FormValidation' => 'errorgen', 'ExternalErrors' => 1);
$form_container = $form->make_container();
$form_container->add_text_field('Text', 'Text field', 'length(4,34)', 'Default' => 'params.Username',
			'Size' => 26, 'MaxLength' => 30, 'Attributes' => 'class="css_style"');
$form_container->add_checkbox('Random', 'Be a bit more random');
$form_container->add_number_field('NumField', 'A number', 'range(-2,302)', '-1', '', 'Size' => '15', 'BlankValue' => -12);
$form_container->add_number_field('NumFieldFP', 'A fixed point number', 'range(-2.8,302.89)', '-1', '', 'Size' => '15',
	'FixedPointScaleDigits' => 4, 'FixedPointDisplayDigits' => 2, 'BlankValue' => -12);
$form_container->add_choice('Thingy', 'Colour of bathtub', 'White|Black|Purple|Blue', 'items', 'single', '');
$form_container->add_choice('X2', 'Test?', 'Yes|No|Only on Tuesdays', 'select', 'single', 'CONSTANT:Yes');
# make sure there are 9 choices to test the code for looping over C++ constants for long lists
$form_container->add_choice('OneOrTwo', 'Choose one or two', 'Pineapple|Orange|Banana|Apple|Lemon|Boat|Plane|Pants|Trousers', 'select', 'choices(1,2)', '');
$form_container->add_choice('EmitErrors', 'Emit errors', 'one|two', 'items', 'choices(,)', '');
$form_container->add_submit_button('DefaultButton', 'Submit the form');


my $cookie_form = WebAppFramework::Unit::Form->new('FormName' => 'cookieForm',
	'HandleSubmission' => "WAF::SetCookie('TestCookie', 'cookieForm.CookieValue');\n");
my $cform_container = $cookie_form->make_container();
$cform_container->add_text_field('CookieValue', 'Value of cookie', 'length(1,32)', 'Default' => 'cookie.TestCookie');
$cform_container->add_submit_button('DefaultButton', 'Change cookie');

# move the error display to after the form, just to demonstrate it's possible
#$form->add_post_unit(WebAppFramework::Unit::FormErrorDisplay->new('Form' => $form));

# -------------------

# build a form where some items are optional
my $optformlinks = WebAppFramework::Unit::ListOfLinks->new(
	'Links' => 
		[
			['All', ['Main', 'FormShow' => 'CONSTANT:0']],
			['Not BC', ['Main', 'FormShow' => 'CONSTANT:1']],
			['Not F-H (in container)', ['Main', 'FormShow' => 'CONSTANT:2']],
		]);

my $optform = WebAppFramework::Unit::Form->new('FormName' => 'formshow');
# containers
$optform_container2 = $optform->make_container();
$optform_container = $optform->make_container();
# fill containers
for('A' .. 'E')
{
	$optform_container->add_text_field($_, $_, 'length(4,34)');
}
$optform_container->add_submit_button('DefaultButton', 'Submit the form');
# another container
for('F' .. 'H')
{
	$optform_container2->add_text_field($_, $_, 'length(4,34)');
}

# make things conditional
$optform->conditional_items('params.GetFormShow() != 1', 'B' => undef, 'C' => 'not shown');
$optform->conditional_items('params.GetFormShow() != 2', $optform_container2 => 'container was here<br>');

# -------------------

# put them all together on the page
$page->add_unit('PAGE', join_units('<br>&nbsp<br>',
	$form,
	"NumField = {widgets.NumField}, NumFieldFP = {widgets.NumFieldFP} (raw, formatted is below)",
	WebAppFramework::Unit::FixedPointNumber->new('Variable' => 'widgets.NumFieldFP',
			'ScaleDigits' => 4, 'DisplayDigits' => 2),
	$table,
	$optformlinks,
	$optform,
	$cookie_form,
	<<__E,
	<p>This is a long paragraph of text.
	Multiple lines.</p>
__E
	<<__E
	*userid_2*<p>This is another long paragraph.
	Multiple lines, and the text is given a user id for keying the translations.</p>
__E
	)
);

# test the other pseudo functions, and putting arbitary code into the output
# (WAF::SetCookie used above)
my $code = WebAppFramework::Unit::Code->new('Phase' => 'lang_prepare', 'Code' => <<__E);
	{
		std::string link(WAF::Link('DisplayEntry', 'EntryID' => 'widgets.NumField'));
		std::string var(WAF::Var('widgets.Text'));
		std::string var2((std::string)WAF::Var('widgets.NumField'));
	}
__E
$page->add_pre_unit($code);

1;
