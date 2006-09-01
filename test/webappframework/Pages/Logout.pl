use WebAppFramework::Unit::Code;

$page->add_text('TITLE', 'Log out');
$page->add_text('PAGE', 'You have been logged out');

my $logout = WebAppFramework::Unit::Code->new('Phase' => 'lang_prepare', 'Code' => <<__E);
	rResponse.SetCookie("Credentials", "");
__E
# add anywhere
$page->add_post_unit($logout);
