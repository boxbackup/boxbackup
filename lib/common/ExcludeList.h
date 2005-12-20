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

	void AddDefiniteEntries(const std::string &rEntries);
	void AddRegexEntries(const std::string &rEntries);

	// Add exceptions to the exclusions (takes ownership)
	void SetAlwaysIncludeList(ExcludeList *pAlwaysInclude);
	
	// Test function
	bool IsExcluded(const std::string &rTest) const;
	
	// Mainly for tests
	size_t SizeOfDefiniteList() const {return mDefinite.size();}
	size_t SizeOfRegexList() const
#ifdef HAVE_REGEX_H
		{return mRegex.size();}
#else
		{return 0;}
#endif

private:
	std::set<std::string> mDefinite;
#ifdef HAVE_REGEX_H
	std::vector<regex_t *> mRegex;
#endif

	// For exceptions to the excludes
	ExcludeList *mpAlwaysInclude;
};

#endif // EXCLUDELIST__H

