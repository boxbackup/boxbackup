use WebAppFramework::Unit::Database::Table;
use Database::Query;
use WebAppFramework::Unit::ListOfLinks;

$page->add_text('TITLE', 'Display entry');

# The query to fetch the entire list out of the database
my $query = Database::Query->new(
		'Name' => 'ListEntriesRetrieve',
		'Statement' => 'SELECT fID,fName,fString,fInteger,fColourName,fBoolean,fCreatedBy FROM tItems LEFT JOIN tColours ON tItems.fColour=tColours.fColourID ORDER BY fString',
		'Results' => 'int32_t ID,std::string Name,std::string String,int32_t Integer,std::string Colour,int32_t Boolean,std::string CreatedBy'
	);

# Unit to display all the entries
# NOTE: If the query above had parameters, we'd specify them with the 'Args' attribute
# to this object, just like you do with a WebAppFramework::Unit::Database::ExecuteQuery object.
my $list = WebAppFramework::Unit::Database::Table->new(
		'Name' => 'entry',
		'Query' => $query,
		'HideFields' => ['ID']	# don't display the ID from the query
	);

# change heading text from the default
$list->add_text('Colour_Heading', 'Item colour');

# To change how a cell is displayed, add an Name_Display unit.
# In this example, to change how ID was displayed, we'd add a unit for
# 'ID_Display'. If that unit needed to know what the ID was, then the page
# variable entry.ID would show this. Here's a trivial example, for a different
# field, which results in exactly the same output as the default, both on the
# page and in the generated C++ code.
use WebAppFramework::Unit::Variable;
$list->add_unit('CreatedBy_Display', WebAppFramework::Unit::Variable->new('Variable' => 'entry.CreatedBy'));

# Could add a column with heading inbetween two columns like this:
# use WebAppFramework::Unit::RawHTML;
# $list->add_unit('Column_Before_Integer', WebAppFramework::Unit::RawHTML->new('HTML' => 'UNIT HERE'));
# $list->add_unit('Column_Before_Integer_Heading', WebAppFramework::Unit::RawHTML->new('HTML' => 'Extra Heading'));

# add some action links

$list->add_unit('Column_Last', WebAppFramework::Unit::ListOfLinks->new(
	'Links' => 
		[
			['Display', ['DisplayEntry', 'EntryID' => 'entry.ID']],
			['Delete', ['DeleteEntry', 'EntryID' => 'entry.ID']],
			['Edit', ['NewEntry', 'EditEntryID' => 'entry.ID']]
		],
	# add an arbitary HTML attribute
	'class' => 'action'
	));

# display the table on the page
$page->add_unit('PAGE', $list);
