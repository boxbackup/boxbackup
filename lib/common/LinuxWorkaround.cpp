// --------------------------------------------------------------------------
//
// File
//		Name:    LinuxWorkaround.cpp
//		Purpose: Workarounds for Linux
//		Created: 2003/10/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <string>

#include "LinuxWorkaround.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

#ifdef PLATFORM_LINUX

// --------------------------------------------------------------------------
//
// Function
//		Name:    LinuxWorkaround_FinishDirentStruct(struct dirent *, const char *)
//		Purpose: Finishes off filling in a dirent structure, which Linux leaves incomplete.
//		Created: 2003/10/31
//
// --------------------------------------------------------------------------
void LinuxWorkaround_FinishDirentStruct(struct dirent *entry, const char *DirectoryName)
{
	// From man readdir under Linux:
	//
	// BUGS
    //   Field d_type is not implemented as  of  libc6  2.1  and  will  always  return
    //   DT_UNKNOWN (0).
	//
	// What kind of an OS is this?
	
	
	// Build filename of this entry
	std::string fn(DirectoryName);
	fn += '/';
	fn += entry->d_name;
	
	// Do a stat on it
	struct stat st;
	if(::lstat(fn.c_str(), &st) != 0)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
	// Fill in the d_type field.
	if(S_ISREG(st.st_mode))
	{
		entry->d_type = DT_REG;	
	}
	else if(S_ISDIR(st.st_mode))
	{
		entry->d_type = DT_DIR;
	}
	else if(S_ISLNK(st.st_mode))
	{
		entry->d_type = DT_LNK;
	}
	// otherwise leave it as we found it
}

#endif // PLATFORM_LINUX

