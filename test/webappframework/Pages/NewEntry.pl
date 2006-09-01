use WebAppFramework::Unit::Form;
use WebAppFramework::Unit::Database::OnSubmitExecSQL;
use WebAppFramework::Unit::Database::FormNewOrEdit;
use WebAppFramework::Unit::DataSource::DatabaseQuery;
use WebAppFramework::Unit::OutputIf;

# Now this can either create a new or edit an entry


$page->add_text('TITLE', 'New/edit entry');

# Create a form (with error validation)
my $form = WebAppFramework::Unit::Form->new('FormName' => 'newentry');
$form_container = $form->make_container();
$form_container->add_text_field('Name', 'Name of item', 'length(4,34)');
$form_container->add_text_field('String', 'A string', 'length(4,34)');
$form_container->add_number_field('Integer', 'An integer', 'range(1,)');
$form_container->add_checkbox('Boolean', 'Boolean flag');

# create a data source to extract the colours from the database
my $colours = WebAppFramework::Unit::DataSource::DatabaseQuery->new(
		Query => {'Name' => 'GetColours',
					'Statement' => 'SELECT fColourID,fColourName FROM tColours ORDER BY fColourID',
					'Results' => 'int32_t ColourID,std::string ColourName'
				}
	);
$form_container->add_choice('Colour', 'Colour', $colours, 'items', 'single', '');
$form_container->add_submit_button('DefaultButton', 'Add entry');
# Find the button, and make the label conditional on the type of form
my $button = $form->find_unit('SubmitButton', 'Name'=>'DefaultButton');
$button->add_unit('Label', WebAppFramework::Unit::OutputIf->new('Condition' => 'params.GetEditEntryID() == 0',
	'@true' => 'Add new entry',
	'@false' => 'Save changes'));


if(0)
{
	# a test of the ability to use strings as keys for selections
	my $colours2 = WebAppFramework::Unit::DataSource::DatabaseQuery->new(
			Query => {'Name' => 'GetColours2',
						'Statement' => 'SELECT fColourName,fColourID FROM tColours ORDER BY fColourName',
						'Results' => 'std::string ColourName,int32_t ColourID'
					}
		);
	$form_container->add_choice('ColourID', 'ColourID', $colours2, 'select', 'single', '');
	$form->add_post_unit(WebAppFramework::Unit::Variable->new('Variable' => 'newentry.ColourID'));
}





my $new_or_edit = WebAppFramework::Unit::Database::FormNewOrEdit->new(
	'@form' => $form,
	'NewCondition' => 'params.GetEditEntryID() == 0',
	'QueryNew' => {
		'Name' => 'NewEntryInsert',
		'Statement' => 'INSERT INTO tItems(fName,fString,fInteger,fColour,fBoolean,fCreatedBy) VALUES($1,$2,$3,$4,$5,$6)',
		'Parameters' => 'std::string Name,std::string String,int32_t Integer,int32_t Colour,int32_t Boolean,std::string CreatedBy',
#		'AutoIncrementValue' => 'tItems fID'
	},
	'ArgsNew' => ['CreatedBy' => 'params.Username'],
	# be perverse, and use two statements to read the data out
	'QueryRead' => [
		{
			'Statement' => 'SELECT fString,fInteger FROM tItems WHERE fID=$1',
			'Parameters' => 'int32_t EntryID',
			'Results' => 'std::string String,int32_t Integer',
		},
		{
			'Statement' => 'SELECT fColour,fName,fBoolean FROM tItems WHERE fID=$1',
			'Parameters' => 'int32_t EntryID',
			'Results' => 'int32_t Colour,std::string Name,int32_t Boolean',
		}
	],
	'ArgsRead' => ['EntryID' => 'params.EditEntryID'],
	'ReadQueryNamespace' => 'original',		# make the results of these queries available as page vars
	'RedirectOnNoReadResults' => ['ListEntries'],
	'QueryUpdate' => {
		'Statement' => 'UPDATE tItems SET fString=$1,fInteger=$2,fColour=$3,fCreatedBy=$4,fBoolean=$5 WHERE fID=$6',
		'Parameters' => 'std::string String,int32_t Integer,int32_t Colour,std::string CreatedBy,int32_t Boolean,int32_t EntryID'
	},
	'ArgsUpdate' => ['EntryID' => 'params.EditEntryID', 'CreatedBy' => 'params.Username'],
	'RedirectTo' => ['ListEntries'],
	# Don't display the (uneditable) name field in edit mode
	'ItemOverrideForUpdate' => ['Name' => '{original.Name}']
);

$page->add_unit('PAGE', $new_or_edit);


1;
