// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientMakeExcludeList.h
//		Purpose: Makes exclude lists from bbbackupd config location entries
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTMAKEEXCLUDELIST__H
#define BACKUPCLIENTMAKEEXCLUDELIST__H

class ExcludeList;
class Configuration;

ExcludeList *BackupClientMakeExcludeList(const Configuration &rConfig, const char *DefiniteName, const char *RegexName,
	const char *AlwaysIncludeDefiniteName = 0, const char *AlwaysIncludeRegexName = 0);

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientMakeExcludeList_Files(const Configuration &)
//		Purpose: Create a exclude list from config file entries for files. May return 0.
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
inline ExcludeList *BackupClientMakeExcludeList_Files(const Configuration &rConfig)
{
	return BackupClientMakeExcludeList(rConfig, "ExcludeFile", "ExcludeFilesRegex", "AlwaysIncludeFile", "AlwaysIncludeFilesRegex");
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientMakeExcludeList_Dirs(const Configuration &)
//		Purpose: Create a exclude list from config file entries for directories. May return 0.
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
inline ExcludeList *BackupClientMakeExcludeList_Dirs(const Configuration &rConfig)
{
	return BackupClientMakeExcludeList(rConfig, "ExcludeDir", "ExcludeDirsRegex", "AlwaysIncludeDir", "AlwaysIncludeDirsRegex");
}


#endif // BACKUPCLIENTMAKEEXCLUDELIST__H

