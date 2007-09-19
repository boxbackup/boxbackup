// --------------------------------------------------------------------------
//
// File
//		Name:    ExcludeList.h
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#ifndef EXCLUDELIST__H
#define EXCLUDELIST__H

#include <string>
#include <set>
#include <vector>

// avoid including regex.h in lots of places
#ifndef EXCLUDELIST_IMPLEMENTATION_REGEX_T_DEFINED
	typedef int regex_t;
#endif

class Archive;

// --------------------------------------------------------------------------
//
// Class
//		Name:    ExcludeList
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
class ExcludeList
{
public:
	ExcludeList();
	~ExcludeList();

	void Deserialize(Archive & rArchive);
	void Serialize(Archive & rArchive) const;

	void AddDefiniteEntries(const std::string &rEntries);
	void AddRegexEntries(const std::string &rEntries);

	// Add exceptions to the exclusions (takes ownership)
	void SetAlwaysIncludeList(ExcludeList *pAlwaysInclude);
	
	// Test function
	bool IsExcluded(const std::string &rTest) const;
	
	// Mainly for tests
	unsigned int SizeOfDefiniteList() const {return mDefinite.size();}
	unsigned int SizeOfRegexList() const
#ifdef HAVE_REGEX_SUPPORT
		{return mRegex.size();}
#else
		{return 0;}
#endif

private:
	std::set<std::string> mDefinite;
#ifdef HAVE_REGEX_SUPPORT
	std::vector<regex_t *> mRegex;
	std::vector<std::string> mRegexStr;	// save original regular expression string-based source for Serialize
#endif

#ifdef WIN32
	std::string ReplaceSlashesDefinite(const std::string& input) const;
	std::string ReplaceSlashesRegex   (const std::string& input) const;
#endif	

	// For exceptions to the excludes
	ExcludeList *mpAlwaysInclude;
};

#endif // EXCLUDELIST__H

