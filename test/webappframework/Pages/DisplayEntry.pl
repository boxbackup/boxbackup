use WebAppFramework::Unit::Database::ExecuteQuery;
use WebAppFramework::Unit::TableContainer;
use WebAppFramework::Unit::Variable;
use Database::Query;

$page->add_text('TITLE', 'Display entry');

# The query to fetch the info out of the database
my $query = Database::Query->new(
		'Name' => 'DisplayEntryRetrieve',
		'Statement' => 'SELECT fID,fString,fInteger,fColour,fCreatedBy FROM tItems WHERE fID=$1',
		'Parameters' => 'int ID',
		'Results' => 'int32_t ID,std::string String,int32_t Integer,int32_t Colour,std::string CreatedBy'
	);

# A unit which will make sure it's executed
my $execquery = WebAppFramework::Unit::Database::ExecuteQuery->new(
		'Name' => 'entry',
		'Query' => $query,
		'Args' => [ID => 'params.EntryID'],
		# if there's no data returned, redirect somewhere harmless
		'RedirectToOnNoRow' => ['Login']
	);

# add it to the end of the page as a post unit, so it gets executed last (in the setup phase, of course)
$page->add_post_unit($execquery);

# display the information in a table
my $table = WebAppFramework::Unit::TableContainer->new('FragmentsName' => 'Table');

# quick function to make things easier
my $row = 0;
sub add_en
{
	my ($label,$var) = @_;
	$table->add_text('0_'.$row, $label);
	$table->add_unit('1_'.$row, WebAppFramework::Unit::Variable->new('Variable' => $var));
	$row++
}

# add all the data to the table, using the helper function just defined
add_en('ID', 'entry.ID');
add_en('A string', 'entry.String');
add_en('An integer', 'entry.Integer');
add_en('Colour', 'entry.Colour');
add_en('Created by', 'entry.CreatedBy');

# display the table on the page
$page->add_unit('PAGE', $table);

1;
