// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreAccountDatabase.cpp
//		Purpose: Database of accounts for the backup store
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <string>
#include <map>
#include <stdio.h>
#include <sys/stat.h>

#include "BackupStoreAccountDatabase.h"
#include "Guards.h"
#include "FdGetLine.h"
#include "BackupStoreException.h"
#include "CommonException.h"
#include "FileModificationTime.h"

#include "MemLeakFindOn.h"

class _BackupStoreAccountDatabase
{
public:
	std::string mFilename;
	std::map<int32_t, BackupStoreAccountDatabase::Entry> mDatabase;
	box_time_t mModificationTime;
};

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::BackupStoreAccountDatabase(const char *)
//		Purpose: Constructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::BackupStoreAccountDatabase(const char *Filename)
	: pImpl(new _BackupStoreAccountDatabase)
{
	pImpl->mFilename = Filename;
	pImpl->mModificationTime = 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::~BackupStoreAccountDatabase()
//		Purpose: Destructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::~BackupStoreAccountDatabase()
{
	delete pImpl;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Entry::Entry()
//		Purpose: Default constructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::Entry::Entry()
	: mID(-1),
	  mDiscSet(-1)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Entry::Entry(int32_t, int)
//		Purpose: Constructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::Entry::Entry(int32_t ID, int DiscSet)
	: mID(ID),
	  mDiscSet(DiscSet)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Entry::Entry(const Entry &)
//		Purpose: Copy constructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::Entry::Entry(const Entry &rEntry)
	: mID(rEntry.mID),
	  mDiscSet(rEntry.mDiscSet)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Entry::~Entry()
//		Purpose: Destructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccountDatabase::Entry::~Entry()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Read(const char *)
//		Purpose: Read in a database from disc
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreAccountDatabase> BackupStoreAccountDatabase::Read(const char *Filename)
{
	// Database object to use
	std::auto_ptr<BackupStoreAccountDatabase> db(new BackupStoreAccountDatabase(Filename));
	
	// Read in the file
	db->ReadFile();

	// Return to called
	return db;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::ReadFile()
//		Purpose: Read the file off disc
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::ReadFile() const
{
	// Open file
	FileHandleGuard<> file(pImpl->mFilename.c_str());

	// Clear existing entries
	pImpl->mDatabase.clear();

	// Read in lines
	FdGetLine getLine(file);
	
	while(!getLine.IsEOF())
	{
		// Read and split up line
		std::string l(getLine.GetLine(true));

		if(!l.empty())
		{
			// Check...
			int32_t id;
			int discSet;
			if(::sscanf(l.c_str(), "%x:%d", &id, &discSet) != 2)
			{
				THROW_EXCEPTION(BackupStoreException, BadAccountDatabaseFile)
			}

			// Make a new entry
			pImpl->mDatabase[id] = Entry(id, discSet);
		}
	}
	
	// Store the modification time of the file
	pImpl->mModificationTime = GetDBFileModificationTime();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::CheckUpToDate()
//		Purpose: Private. Ensure that the in memory database matches the one on disc
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::CheckUpToDate() const
{
	if(pImpl->mModificationTime != GetDBFileModificationTime())
	{
		// File has changed -- load it in again
		ReadFile();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::GetDBFileModificationTime()
//		Purpose: Get the current modification time of the database
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
box_time_t BackupStoreAccountDatabase::GetDBFileModificationTime() const
{
	EMU_STRUCT_STAT st;
	if(EMU_STAT(pImpl->mFilename.c_str(), &st) == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
	return FileModificationTime(st);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::Write()
//		Purpose: Write the database back to disc after modifying it
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::Write()
{
	// Open file for writing
	// Would use this...
	//	FileHandleGuard<O_WRONLY | O_TRUNC> file(pImpl->mFilename.c_str());
	// but gcc fails randomly on it on some platforms. Weird.
	
	int file = ::open(pImpl->mFilename.c_str(), O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(file == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
	
	try
	{
		// Then write each entry
		for(std::map<int32_t, BackupStoreAccountDatabase::Entry>::const_iterator i(pImpl->mDatabase.begin());
			i != pImpl->mDatabase.end(); ++i)
		{
			// Write out the entry
			char line[256];	// more than enough for a couple of integers in string form
			int s = ::sprintf(line, "%x:%d\n", i->second.GetID(), i->second.GetDiscSet());
			if(::write(file, line, s) != s)
			{
				THROW_EXCEPTION(CommonException, OSFileError)
			}
		}
		
		::close(file);
	}
	catch(...)
	{
		::close(file);
		throw;
	}
	
	// Done.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::EntryExists(int32_t)
//		Purpose: Does an entry exist in the database?
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool BackupStoreAccountDatabase::EntryExists(int32_t ID) const
{
	// Check that we're using the latest version of the database
	CheckUpToDate();

	return pImpl->mDatabase.find(ID) != pImpl->mDatabase.end();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::GetEntry(int32_t)
//		Purpose: Retrieve an entry
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
const BackupStoreAccountDatabase::Entry &BackupStoreAccountDatabase::GetEntry(int32_t ID) const
{
	// Check that we're using the latest version of the database
	CheckUpToDate();

	std::map<int32_t, BackupStoreAccountDatabase::Entry>::const_iterator i(pImpl->mDatabase.find(ID));
	if(i == pImpl->mDatabase.end())
	{
		THROW_EXCEPTION(BackupStoreException, AccountDatabaseNoSuchEntry)
	}
	
	return i->second;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::AddEntry(int32_t, int)
//		Purpose: Add a new entry to the database
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::AddEntry(int32_t ID, int DiscSet)
{
	// Check that we're using the latest version of the database
	CheckUpToDate();

	pImpl->mDatabase[ID] = Entry(ID, DiscSet);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::DeleteEntry(int32_t)
//		Purpose: Delete an entry from the database
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::DeleteEntry(int32_t ID)
{
	// Check that we're using the latest version of the database
	CheckUpToDate();

	std::map<int32_t, BackupStoreAccountDatabase::Entry>::iterator i(pImpl->mDatabase.find(ID));
	if(i == pImpl->mDatabase.end())
	{
		THROW_EXCEPTION(BackupStoreException, AccountDatabaseNoSuchEntry)
	}

	pImpl->mDatabase.erase(i);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccountDatabase::GetAllAccountIDs(std::vector<int32_t>)
//		Purpose: 
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void BackupStoreAccountDatabase::GetAllAccountIDs(std::vector<int32_t> &rIDsOut)
{
	// Check that we're using the latest version of the database
	CheckUpToDate();

	// Delete everything in the output list
	rIDsOut.clear();

	std::map<int32_t, BackupStoreAccountDatabase::Entry>::iterator i(pImpl->mDatabase.begin());
	for(; i != pImpl->mDatabase.end(); ++i)
	{
		rIDsOut.push_back(i->first);
	}
}




