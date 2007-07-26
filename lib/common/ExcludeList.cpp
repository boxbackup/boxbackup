// --------------------------------------------------------------------------
//
// File
//		Name:    ExcludeList.cpp
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_PCREPOSIX_H
	#include <pcreposix.h>
	#define EXCLUDELIST_IMPLEMENTATION_REGEX_T_DEFINED
#elif defined HAVE_REGEX_H
	#include <regex.h>
	#define EXCLUDELIST_IMPLEMENTATION_REGEX_T_DEFINED
#endif

#include "ExcludeList.h"
#include "Utils.h"
#include "Configuration.h"
#include "Archive.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::ExcludeList()
//		Purpose: Constructor. Generates an exclude list which will allow everything
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
ExcludeList::ExcludeList()
	: mpAlwaysInclude(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::~ExcludeList()
//		Purpose: Destructor
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
ExcludeList::~ExcludeList()
{
#ifdef HAVE_REGEX_H
	// free regex memory
	while(mRegex.size() > 0)
	{
		regex_t *pregex = mRegex.back();
		mRegex.pop_back();
		// Free regex storage, and the structure itself
		::regfree(pregex);
		delete pregex;
	}
#endif

	// Clean up exceptions list
	if(mpAlwaysInclude != 0)
	{
		delete mpAlwaysInclude;
		mpAlwaysInclude = 0;
	}
}

#ifdef WIN32
std::string ExcludeList::ReplaceSlashesDefinite(const std::string& input) const
{
	std::string output = input;

	for (std::string::size_type pos = output.find("/");
		pos != std::string::npos; 
		pos = output.find("/"))
	{
		output.replace(pos, 1, DIRECTORY_SEPARATOR);
	}

	for (std::string::iterator i = output.begin(); i != output.end(); i++)
	{
		*i = tolower(*i);
	}

	return output;
}

std::string ExcludeList::ReplaceSlashesRegex(const std::string& input) const
{
	std::string output = input;

	for (std::string::size_type pos = output.find("/");
		pos != std::string::npos; 
		pos = output.find("/"))
	{
		output.replace(pos, 1, "\\" DIRECTORY_SEPARATOR);
	}

	for (std::string::iterator i = output.begin(); i != output.end(); i++)
	{
		*i = tolower(*i);
	}

	return output;
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::AddDefiniteEntries(const std::string &)
//		Purpose: Adds a number of definite entries to the exclude list -- ones which
//				 will be excluded if and only if the test string matches exactly.
//				 Uses the Configuration classes' multi-value conventions, with
//				 multiple entires in one string separated by Configuration::MultiValueSeparator
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
void ExcludeList::AddDefiniteEntries(const std::string &rEntries)
{
	// Split strings up
	std::vector<std::string> ens;
	SplitString(rEntries, Configuration::MultiValueSeparator, ens);
	
	// Add to set of excluded strings
	for(std::vector<std::string>::const_iterator i(ens.begin()); i != ens.end(); ++i)
	{
		if(i->size() > 0)
		{
			std::string entry = *i;

			// Convert any forward slashes in the string
			// to backslashes

			#ifdef WIN32
			entry = ReplaceSlashesDefinite(entry);
			#endif

			if (entry.size() > 0 && entry[entry.size() - 1] == 
				DIRECTORY_SEPARATOR_ASCHAR)
			{
				BOX_WARNING("Exclude entry ends in path "
					"separator, will never match: " 
					<< entry);
			}

			mDefinite.insert(entry);
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::AddRegexEntries(const std::string &)
//		Purpose: Adds a number of regular expression entries to the exclude list -- 
//				 if the test expression matches any of these regex, it will be excluded.
//				 Uses the Configuration classes' multi-value conventions, with
//				 multiple entires in one string separated by Configuration::MultiValueSeparator
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
void ExcludeList::AddRegexEntries(const std::string &rEntries)
{
#ifdef HAVE_REGEX_H

	// Split strings up
	std::vector<std::string> ens;
	SplitString(rEntries, Configuration::MultiValueSeparator, ens);
	
	// Create and add new regular expressions
	for(std::vector<std::string>::const_iterator i(ens.begin()); i != ens.end(); ++i)
	{
		if(i->size() > 0)
		{
			// Allocate memory
			regex_t *pregex = new regex_t;
			
			try
			{
				std::string entry = *i;

				// Convert any forward slashes in the string
				// to appropriately escaped backslashes

				#ifdef WIN32
				entry = ReplaceSlashesRegex(entry);
				#endif

				// Compile
				if(::regcomp(pregex, entry.c_str(), 
					REG_EXTENDED | REG_NOSUB) != 0)
				{
					THROW_EXCEPTION(CommonException, BadRegularExpression)
				}
				
				// Store in list of regular expressions
				mRegex.push_back(pregex);
				// Store in list of regular expression string for Serialize
				mRegexStr.push_back(entry.c_str());
			}
			catch(...)
			{
				delete pregex;
				throw;
			}
		}
	}

#else
	THROW_EXCEPTION(CommonException, RegexNotSupportedOnThisPlatform)
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::IsExcluded(const std::string &)
//		Purpose: Returns true if the entry should be excluded
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
bool ExcludeList::IsExcluded(const std::string &rTest) const
{
	std::string test = rTest;

	#ifdef WIN32
	test = ReplaceSlashesDefinite(test);
	#endif

	// Check against the always include list
	if(mpAlwaysInclude != 0)
	{
		if(mpAlwaysInclude->IsExcluded(test))
		{
			// Because the "always include" list says it's 'excluded'
			// this means it should actually be included.
			return false;
		}
	}

	// Is it in the set of definite entries?
	if(mDefinite.find(test) != mDefinite.end())
	{
		return true;
	}
	
	// Check against regular expressions
#ifdef HAVE_REGEX_H
	for(std::vector<regex_t *>::const_iterator i(mRegex.begin()); i != mRegex.end(); ++i)
	{
		// Test against this expression
		if(regexec(*i, test.c_str(), 0, 0 /* no match information required */, 0 /* no flags */) == 0)
		{
			// match happened
			return true;
		}
		// In all other cases, including an error, just continue to the next expression
	}
#endif

	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::SetAlwaysIncludeList(ExcludeList *)
//		Purpose: Takes ownership of the list, deletes any pre-existing list.
//				 NULL is acceptable to delete the list.
//				 The AlwaysInclude list is a list of exceptions to the exclusions.
//		Created: 19/2/04
//
// --------------------------------------------------------------------------
void ExcludeList::SetAlwaysIncludeList(ExcludeList *pAlwaysInclude)
{
	// Delete old list
	if(mpAlwaysInclude != 0)
	{
		delete mpAlwaysInclude;
		mpAlwaysInclude = 0;
	}
	
	// Store the pointer
	mpAlwaysInclude = pAlwaysInclude;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::Deserialize(Archive & rArchive)
//		Purpose: Deserializes this object instance from a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void ExcludeList::Deserialize(Archive & rArchive)
{
	//
	//
	//
	mDefinite.clear();

#ifdef HAVE_REGEX_H
	// free regex memory
	while(mRegex.size() > 0)
	{
		regex_t *pregex = mRegex.back();
		mRegex.pop_back();
		// Free regex storage, and the structure itself
		::regfree(pregex);
		delete pregex;
	}

	mRegexStr.clear();
#endif

	// Clean up exceptions list
	if(mpAlwaysInclude != 0)
	{
		delete mpAlwaysInclude;
		mpAlwaysInclude = 0;
	}

	//
	//
	//
	int64_t iCount = 0;
	rArchive.Read(iCount);

	if (iCount > 0)
	{
		for (int v = 0; v < iCount; v++)
		{
			// load each one
			std::string strItem;
			rArchive.Read(strItem);
			mDefinite.insert(strItem);
		}
	}

	//
	//
	//
#ifdef HAVE_REGEX_H
	rArchive.Read(iCount);

	if (iCount > 0)
	{
		for (int v = 0; v < iCount; v++)
		{
			std::string strItem;
			rArchive.Read(strItem);

			// Allocate memory
			regex_t* pregex = new regex_t;
			
			try
			{
				// Compile
				if(::regcomp(pregex, strItem.c_str(), 
					REG_EXTENDED | REG_NOSUB) != 0)
				{
					THROW_EXCEPTION(CommonException, 
						BadRegularExpression)
				}
				
				// Store in list of regular expressions
				mRegex.push_back(pregex);

				// Store in list of regular expression strings
				// for Serialize
				mRegexStr.push_back(strItem);
			}
			catch(...)
			{
				delete pregex;
				throw;
			}
		}
	}
#endif // HAVE_REGEX_H

	//
	//
	//
	int64_t aMagicMarker = 0;
	rArchive.Read(aMagicMarker);

	if (aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if (aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mpAlwaysInclude = new ExcludeList;
		if (!mpAlwaysInclude)
		{
			throw std::bad_alloc();
		}

		mpAlwaysInclude->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(CommonException, Internal)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::Serialize(Archive & rArchive)
//		Purpose: Serializes this object instance into a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void ExcludeList::Serialize(Archive & rArchive) const
{
	//
	//
	//
	int64_t iCount = mDefinite.size();
	rArchive.Write(iCount);

	for (std::set<std::string>::const_iterator i = mDefinite.begin(); 
		i != mDefinite.end(); i++)
	{
		rArchive.Write(*i);
	}

	//
	//
	//
#ifdef HAVE_REGEX_H
	// don't even try to save compiled regular expressions,
	// use string copies instead.
	ASSERT(mRegex.size() == mRegexStr.size()); 	

	iCount = mRegexStr.size();
	rArchive.Write(iCount);

	for (std::vector<std::string>::const_iterator i = mRegexStr.begin(); 
		i != mRegexStr.end(); i++)
	{
		rArchive.Write(*i);
	}
#endif // HAVE_REGEX_H

	//
	//
	//
	if (!mpAlwaysInclude)
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mpAlwaysInclude->Serialize(rArchive);
	}
}
