use WebAppFramework::Unit::Database::ExecuteQuery;
use Database::Query;

$page->add_text('TITLE', 'Delete entry');

# The query to fetch the info out of the database
my $query = Database::Query->new(
		'Name' => 'DeleteEntry',
		'Statement' => 'DELETE FROM tItems WHERE fID=$1',
		'Parameters' => 'int ID',
		'Results' => 'int32_t ID,std::string String,int32_t Integer,int32_t Colour,std::string CreatedBy'
	);

# A unit which will make sure it's executed
# Using the redirect to the listing page is a bit of a cheat, but will work
# because the query will never return any results... There are more elegant ways
# of doing this which involve custom code...
my $execquery = WebAppFramework::Unit::Database::ExecuteQuery->new(
		'Name' => 'entry',
		'Query' => $query,
		'Args' => [ID => 'params.EntryID'],
		# if there's no data returned, redirect somewhere harmless
		'RedirectToOnNoRow' => ['ListEntries']
	);

# add it to the end of the page as a post unit, so it gets executed last (in the setup phase, of course)
$page->add_post_unit($execquery);

# Put something onto the page to avoid warnings
$page->add_unit('PAGE', WebAppFramework::Unit::RawHTML->new('HTML' => ''));

1;
