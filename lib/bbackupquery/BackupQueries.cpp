// --------------------------------------------------------------------------
//
// File
//		Name:    BackupQueries.cpp
//		Purpose: Perform various queries on the backup store server.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_DIRENT_H
	#include <dirent.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <iostream>
#include <ostream>
#include <set>

#include "BackupClientFileAttributes.h"
#include "BackupClientMakeExcludeList.h"
#include "BackupClientRestore.h"
#include "BackupQueries.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFilenameClear.h"
#include "BoxTimeToText.h"
#include "CommonException.h"
#include "Configuration.h"
#include "ExcludeList.h"
#include "FileModificationTime.h"
#include "FileStream.h"
#include "IOStream.h"
#include "Logging.h"
#include "PathUtils.h"
#include "SelfFlushingStream.h"
#include "Utils.h"
#include "autogen_BackupProtocol.h"
#include "autogen_CipherException.h"

#include "MemLeakFindOn.h"

// min() and max() macros from stdlib.h break numeric_limits<>::min(), etc.
#undef min
#undef max

#define COMPARE_RETURN_SAME		1
#define COMPARE_RETURN_DIFFERENT	2
#define COMPARE_RETURN_ERROR		3
#define COMMAND_RETURN_ERROR		4

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::BackupQueries()
//		Purpose: Constructor
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
BackupQueries::BackupQueries(BackupProtocolCallable &rConnection,
	const Configuration &rConfiguration, bool readWrite)
	: mReadWrite(readWrite),
	  mrConnection(rConnection),
	  mrConfiguration(rConfiguration),
	  mQuitNow(false),
	  mRunningAsRoot(false),
	  mWarnedAboutOwnerAttributes(false),
	  mReturnCode(0)		// default return code
{
	#ifdef WIN32
	mRunningAsRoot = TRUE;
	#else
	mRunningAsRoot = (::geteuid() == 0);
	#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::~BackupQueries()
//		Purpose: Destructor
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
BackupQueries::~BackupQueries()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::DoCommand(const char *, bool)
//		Purpose: Perform a command
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::DoCommand(ParsedCommand& rCommand)
{
	// Check...

	if(rCommand.mFailed)
	{
		BOX_ERROR("Parse failed: unknown command '" << 
			rCommand.mCmdElements[0] << "' or failed to convert "
			"encoding of arguments");
		return;
	}

	if(rCommand.mCmdElements.size() < 1)
	{
		// blank command
		return;
	}

	if(rCommand.pSpec->type == Command_sh &&
		rCommand.mCmdElements.size() == 2)
	{
		// Yes, run shell command
		int result = ::system(rCommand.mCmdElements[1].c_str());
		if(result != 0)
		{
			BOX_WARNING("System command returned error code " <<
				result);
			SetReturnCode(ReturnCode::Command_Error);
		}
		return;
	}
		
	if(rCommand.pSpec->type == Command_Unknown)
	{
		// No such command
		BOX_ERROR("Unrecognised command: " << rCommand.mCmdElements[0]);
		return;
	}

	// Arguments
	std::vector<std::string> args(rCommand.mCmdElements.begin() + 1,
		rCommand.mCmdElements.end());

	// Set up options
	bool opts[256];
	for(int o = 0; o < 256; ++o) opts[o] = false;
	// BLOCK
	{
		// options
		const char *c = rCommand.mOptions.c_str();
		while(*c != 0)
		{
			// Valid option?
			if(::strchr(rCommand.pSpec->opts, *c) == NULL)
			{
				BOX_ERROR("Invalid option '" << *c << "' for "
					"command " << rCommand.pSpec->name);
				return;
			}
			opts[(int)*c] = true;
			++c;
		}
	}

	if(rCommand.pSpec->type != Command_Quit)
	{
		// If not a quit command, set the return code to zero
		SetReturnCode(ReturnCode::Command_OK);
	}

	// Handle command
	switch(rCommand.pSpec->type)
	{
	case Command_Quit:
		mQuitNow = true;
		break;
		
	case Command_List:
		CommandList(args, opts);
		break;
		
	case Command_pwd:
		{
			// Simple implementation, so do it here
			BOX_NOTICE(GetCurrentDirectoryName() << " (" <<
				BOX_FORMAT_OBJECTID(GetCurrentDirectoryID()) <<
				")");
		}
		break;

	case Command_cd:
		CommandChangeDir(args, opts);
		break;
		
	case Command_lcd:
		CommandChangeLocalDir(args);
		break;
		
	case Command_sh:
		BOX_ERROR("The command to run must be specified as an argument.");
		break;
		
	case Command_GetObject:
		CommandGetObject(args, opts);
		break;
		
	case Command_Get:
		CommandGet(args, opts);
		break;
		
	case Command_Compare:
		CommandCompare(args, opts);
		break;
		
	case Command_Restore:
		CommandRestore(args, opts);
		break;
		
	case Command_Usage:
		CommandUsage(opts);
		break;
		
	case Command_Help:
		CommandHelp(args);
		break;

	case Command_Undelete:
		CommandUndelete(args, opts);
		break;
		
	case Command_Delete:
		CommandDelete(args, opts);
		break;
		
	default:
		BOX_ERROR("Unknown command: " << rCommand.mCmdElements[0]);
		break;
	}
}

#define LIST_OPTION_TIMES_ATTRIBS	'a'
#define LIST_OPTION_SORT_NO_DIRS_FIRST	'D'
#define LIST_OPTION_NOFLAGS		'F'
#define LIST_OPTION_DISPLAY_HASH	'h'
#define LIST_OPTION_SORT_ID		'i'
#define LIST_OPTION_NOOBJECTID		'I'
#define LIST_OPTION_SORT_REVERSE	'r'
#define LIST_OPTION_RECURSIVE		'R'
#define LIST_OPTION_SIZEINBLOCKS	's'
#define LIST_OPTION_SORT_SIZE		'S'
#define LIST_OPTION_TIMES_LOCAL		't'
#define LIST_OPTION_TIMES_UTC		'T'
#define LIST_OPTION_SORT_NONE		'U'

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandList(const std::vector<std::string> &, const bool *)
//		Purpose: List directories (optionally recursive)
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::CommandList(const std::vector<std::string> &args, const bool *opts)
{
	// default to using the current directory
	int64_t rootDir = GetCurrentDirectoryID();

	// name of base directory
	std::string listRoot;	// blank

	// Got a directory in the arguments?
	if(args.size() > 0)
	{
#ifdef WIN32
		std::string storeDirEncoded;
		if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded))
			return;
#else
		const std::string& storeDirEncoded(args[0]);
#endif
	
		// Attempt to find the directory
		rootDir = FindDirectoryObjectID(storeDirEncoded,
			opts[LIST_OPTION_ALLOWOLD],
			opts[LIST_OPTION_ALLOWDELETED]);

		if(rootDir == 0)
		{
			BOX_ERROR("Directory '" << args[0] << "' not found "
				"on store.");
			SetReturnCode(ReturnCode::Command_Error);
			return;
		}
	}
	
	// List it
	List(rootDir, listRoot, opts, true /* first level to list */);
}

static std::string GetTimeString(BackupStoreDirectory::Entry& en,
	bool useLocalTime, bool showAttrModificationTimes)
{
	std::ostringstream out;
	box_time_t originalTime, newAttributesTime;

	// there is no attribute modification time in the directory
	// entry, unfortunately, so we can't display it.
	originalTime = en.GetModificationTime();
	out << BoxTimeToISO8601String(originalTime, useLocalTime);

	if(en.HasAttributes())
	{
		const StreamableMemBlock &storeAttr(en.GetAttributes());
		BackupClientFileAttributes attr(storeAttr);
		
		box_time_t NewModificationTime, NewAttrModificationTime;
		attr.GetModificationTimes(&NewModificationTime,
			&NewAttrModificationTime);
		
		if (showAttrModificationTimes)
		{
			newAttributesTime = NewAttrModificationTime;
		}
		else
		{
			newAttributesTime = NewModificationTime;
		}
		
		if (newAttributesTime == originalTime)
		{
			out << "*";
		}
		else
		{
			out << "~" << BoxTimeToISO8601String(newAttributesTime,
				useLocalTime);
		}
	}
	else
	{
		out << " ";
	}
	
	return out.str();
}

/* We need a way to pass options to sort functions for sorting. The algorithm
 * doesn't seem to provide a way to do this, so I'm using a global variable.
 * Which is not thread safe, but we don't currently use threads so that should
 * be OK. Do not use threads without checking!
 */
const bool *gThreadUnsafeOptions;

int DirsFirst(BackupStoreDirectory::Entry* a,
	BackupStoreDirectory::Entry* b)
{
	if (a->IsDir() && !b->IsDir())
	{
		return -1; // a < b
	}
	else if (!a->IsDir() && b->IsDir())
	{
		return 1; // b > a
	}
	else
	{
		return 0; // continue comparison
	}
}

#define MAYBE_DIRS_FIRST(a, b) \
	if (!gThreadUnsafeOptions[LIST_OPTION_SORT_NO_DIRS_FIRST]) \
	{ \
		int result = DirsFirst(a, b); \
		if (result < 0) return true; /* a < b */ \
		else if (result > 0) return false; /* a > b */ \
		/* else: fall through */ \
	}

#define MAYBE_REVERSE(result) \
	(result != gThreadUnsafeOptions[LIST_OPTION_SORT_REVERSE])
// result is false, opts[reverse] is false => return false
// result is false, opts[reverse] is true  => return true
// result is true,  opts[reverse] is false => return true
// result is true,  opts[reverse] is true  => return false
// this is logical XOR, for which the boolean operator is !=.

bool SortById(BackupStoreDirectory::Entry* a,
	BackupStoreDirectory::Entry* b)
{
	MAYBE_DIRS_FIRST(a, b);
	bool result = (a->GetObjectID() < b->GetObjectID());
	return MAYBE_REVERSE(result);
}

bool SortBySize(BackupStoreDirectory::Entry* a,
	BackupStoreDirectory::Entry* b)
{
	MAYBE_DIRS_FIRST(a, b);
	bool result = (a->GetSizeInBlocks() < b->GetSizeInBlocks());
	return MAYBE_REVERSE(result);
}

bool SortByName(BackupStoreDirectory::Entry* a,
	BackupStoreDirectory::Entry* b)
{
	MAYBE_DIRS_FIRST(a, b);
	BackupStoreFilenameClear afc(a->GetName());
	BackupStoreFilenameClear bfc(b->GetName());
	std::string an = afc.GetClearFilename();
	std::string bn = bfc.GetClearFilename();
	bool result = (an < bn);
	return MAYBE_REVERSE(result);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::List(int64_t, const std::string &, const bool *, bool)
//		Purpose: Do the actual listing of directories and files
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::List(int64_t DirID, const std::string &rListRoot,
	const bool *opts, bool FirstLevel, std::ostream* pOut)
{
#ifdef WIN32
	DWORD n_chars;
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
	
	// Generate exclude flags
	int16_t excludeFlags = BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING;
	if(!opts[LIST_OPTION_ALLOWOLD]) excludeFlags |= BackupProtocolListDirectory::Flags_OldVersion;
	if(!opts[LIST_OPTION_ALLOWDELETED]) excludeFlags |= BackupProtocolListDirectory::Flags_Deleted;

	// Do communication
	try
	{
		mrConnection.QueryListDirectory(
			DirID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			// both files and directories
			excludeFlags,
			true /* want attributes */);
	}
	catch (std::exception &e)
	{
		BOX_ERROR("Failed to list directory: " << e.what());
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	catch (...)
	{
		BOX_ERROR("Failed to list directory: unknown error");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

	// Store entry pointers in a std::vector for sorting
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	std::vector<BackupStoreDirectory::Entry*> sorted_entries;
	while((en = i.Next()) != 0)
	{
		sorted_entries.push_back(en);
	}

	// Typedef to avoid mind-bending while dealing with pointers to functions.
	typedef bool (EntryComparator_t)(BackupStoreDirectory::Entry* a,
		BackupStoreDirectory::Entry* b);
	// Default is no comparator, i.e. no sorting.
	EntryComparator_t* pComparator = NULL;

	if (opts[LIST_OPTION_SORT_ID])
	{
		pComparator = &SortById;
	}
	else if (opts[LIST_OPTION_SORT_SIZE])
	{
		pComparator = &SortBySize;
	}
	else if (opts[LIST_OPTION_SORT_NONE])
	{
		// do nothing
	}
	else // sort by name
	{
		pComparator = &SortByName;
	}

	if (pComparator != NULL)
	{
		gThreadUnsafeOptions = opts;
		sort(sorted_entries.begin(), sorted_entries.end(),
			pComparator);
		gThreadUnsafeOptions = NULL;
	}

	for (std::vector<BackupStoreDirectory::Entry*>::const_iterator
		i = sorted_entries.begin();
		i != sorted_entries.end(); i++)
	{
		en = *i;
		std::ostringstream buf;

		// Display this entry
		BackupStoreFilenameClear clear(en->GetName());
		
		// Object ID?
		if(!opts[LIST_OPTION_NOOBJECTID])
		{
			// add object ID to line
			buf << std::hex << std::internal << std::setw(8) <<
				std::setfill('0') << en->GetObjectID() <<
				std::dec << " ";
		}
		
		// Flags?
		if(!opts[LIST_OPTION_NOFLAGS])
		{
			static const char *flags = BACKUPSTOREDIRECTORY_ENTRY_FLAGS_DISPLAY_NAMES;
			char displayflags[16];
			// make sure f is big enough
			ASSERT(sizeof(displayflags) >= sizeof(BACKUPSTOREDIRECTORY_ENTRY_FLAGS_DISPLAY_NAMES) + 3);
			// Insert flags
			char *f = displayflags;
			const char *t = flags;
			int16_t en_flags = en->GetFlags();
			while(*t != 0)
			{
				*f = ((en_flags&1) == 0)?'-':*t;
				en_flags >>= 1;
				f++;
				t++;
			}
			// attributes flags
			*(f++) = (en->HasAttributes())?'a':'-';

			// terminate
			*(f++) = ' ';
			*(f++) = '\0';
			buf << displayflags;
			
			if(en_flags != 0)
			{
				buf << "[ERROR: Entry has additional flags set] ";
			}
		}
		
		if(opts[LIST_OPTION_TIMES_UTC])
		{
			// Show UTC times...
			buf << GetTimeString(*en, false,
				opts[LIST_OPTION_TIMES_ATTRIBS]) << " ";
		}

		if(opts[LIST_OPTION_TIMES_LOCAL])
		{
			// Show local times...
			buf << GetTimeString(*en, true,
				opts[LIST_OPTION_TIMES_ATTRIBS]) << " ";
		}
		
		if(opts[LIST_OPTION_DISPLAY_HASH])
		{
			buf << std::hex << std::internal << std::setw(16) <<
				std::setfill('0') << en->GetAttributesHash() <<
				std::dec;
		}
		
		if(opts[LIST_OPTION_SIZEINBLOCKS])
		{
			buf << std::internal << std::setw(5) <<
				std::setfill('0') << en->GetSizeInBlocks() <<
				" ";
		}
		
		// add name
		if(!FirstLevel)
		{
#ifdef WIN32
			std::string listRootDecoded;
			if(!ConvertUtf8ToConsole(rListRoot.c_str(), 
				listRootDecoded)) return;
			listRootDecoded += "/";
			buf << listRootDecoded;
			WriteConsole(hOut, listRootDecoded.c_str(),
				strlen(listRootDecoded.c_str()), &n_chars, NULL);
#else
			buf << rListRoot << "/";
#endif
		}
		
		std::string fileName;
		try
		{
			fileName = clear.GetClearFilename();
		}
		catch(CipherException &e)
		{
			fileName = "<decrypt failed>";
		}

#ifdef WIN32
		std::string fileNameUtf8 = fileName;
		if(!ConvertUtf8ToConsole(fileNameUtf8, fileName))
		{
			fileName = fileNameUtf8 + " [convert encoding failed]";
		}
#endif

		buf << fileName;

		if(!en->GetName().IsEncrypted())
		{
			buf << " [FILENAME NOT ENCRYPTED]";
		}

		buf << std::endl;
		
		if(pOut)
		{
			(*pOut) << buf.str();
		}
		else
		{
#ifdef WIN32
			std::string line = buf.str();
			if (!WriteConsole(hOut, line.c_str(), line.size(),
				&n_chars, NULL))
			{
				// WriteConsole failed, try standard method
				std::cout << buf.str();
			}
#else
			std::cout << buf.str();
#endif
		}
		
		// Directory?
		if((en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) != 0)
		{
			// Recurse?
			if(opts[LIST_OPTION_RECURSIVE])
			{
				std::string subroot(rListRoot);
				if(!FirstLevel) subroot += '/';
				subroot += clear.GetClearFilename();
				List(en->GetObjectID(), subroot, opts,
					false /* not the first level to list */,
					pOut);
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::FindDirectoryObjectID(const
//			 std::string &)
//		Purpose: Find the object ID of a directory on the store,
//			 or return 0 for not found. If pStack != 0, the
//			 object is set to the stack of directories.
//			 Will start from the current directory stack.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
int64_t BackupQueries::FindDirectoryObjectID(const std::string &rDirName,
	bool AllowOldVersion, bool AllowDeletedDirs,
	std::vector<std::pair<std::string, int64_t> > *pStack)
{
	// Split up string into elements
	std::vector<std::string> dirElements;
	SplitString(rDirName, '/', dirElements);

	// Start from current stack, or root, whichever is required
	std::vector<std::pair<std::string, int64_t> > stack;
	int64_t dirID = BackupProtocolListDirectory::RootDirectory;
	if(rDirName.size() > 0 && rDirName[0] == '/')
	{
		// Root, do nothing
	}
	else
	{
		// Copy existing stack
		stack = mDirStack;
		if(stack.size() > 0)
		{
			dirID = stack[stack.size() - 1].second;
		}
	}

	// Generate exclude flags
	int16_t excludeFlags = BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING;
	if(!AllowOldVersion) excludeFlags |= BackupProtocolListDirectory::Flags_OldVersion;
	if(!AllowDeletedDirs) excludeFlags |= BackupProtocolListDirectory::Flags_Deleted;

	// Read directories
	for(unsigned int e = 0; e < dirElements.size(); ++e)
	{
		if(dirElements[e].size() > 0)
		{
			if(dirElements[e] == ".")
			{
				// Ignore.
			}
			else if(dirElements[e] == "..")
			{
				// Up one!
				if(stack.size() > 0)
				{
					// Remove top element
					stack.pop_back();
					
					// New dir ID
					dirID = (stack.size() > 0)?(stack[stack.size() - 1].second):BackupProtocolListDirectory::RootDirectory;
				}
				else
				{	
					// At root anyway
					dirID = BackupProtocolListDirectory::RootDirectory;
				}
			}
			else
			{
				// Not blank element. Read current directory.
				std::auto_ptr<BackupProtocolSuccess> dirreply(mrConnection.QueryListDirectory(
						dirID,
						BackupProtocolListDirectory::Flags_Dir,	// just directories
						excludeFlags,
						true /* want attributes */));

				// Retrieve the directory from the stream following
				BackupStoreDirectory dir;
				std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
				dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

				// Then... find the directory within it
				BackupStoreDirectory::Iterator i(dir);
				BackupStoreFilenameClear dirname(dirElements[e]);
				BackupStoreDirectory::Entry *en = i.FindMatchingClearName(dirname);
				if(en == 0)
				{
					// Not found
					return 0;
				}
				
				// Object ID for next round of searching
				dirID = en->GetObjectID();

				// Push onto stack
				stack.push_back(std::pair<std::string, int64_t>(dirElements[e], dirID));
			}
		}
	}
	
	// If required, copy the new stack to the caller
	if(pStack)
	{
		*pStack = stack;
	}

	return dirID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::GetCurrentDirectoryID()
//		Purpose: Returns the ID of the current directory
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
int64_t BackupQueries::GetCurrentDirectoryID()
{
	// Special case for root
	if(mDirStack.size() == 0)
	{
		return BackupProtocolListDirectory::RootDirectory;
	}
	
	// Otherwise, get from the last entry on the stack
	return mDirStack[mDirStack.size() - 1].second;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::GetCurrentDirectoryName()
//		Purpose: Gets the name of the current directory
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
std::string BackupQueries::GetCurrentDirectoryName()
{
	// Special case for root
	if(mDirStack.size() == 0)
	{
		return std::string("/");
	}

	// Build path
	std::string r;
	for(unsigned int l = 0; l < mDirStack.size(); ++l)
	{
		r += "/";
#ifdef WIN32
		std::string dirName;
		if(!ConvertUtf8ToConsole(mDirStack[l].first.c_str(), dirName))
			return "error";
		r += dirName;
#else
		r += mDirStack[l].first;
#endif
	}
	
	return r;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandChangeDir(const std::vector<std::string> &)
//		Purpose: Change directory command
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::CommandChangeDir(const std::vector<std::string> &args, const bool *opts)
{
	if(args.size() != 1 || args[0].size() == 0)
	{
		BOX_ERROR("Incorrect usage. cd [-o] [-d] <directory>");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}

#ifdef WIN32
	std::string dirName;
	if(!ConvertConsoleToUtf8(args[0].c_str(), dirName)) return;
#else
	const std::string& dirName(args[0]);
#endif
	
	std::vector<std::pair<std::string, int64_t> > newStack;
	int64_t id = FindDirectoryObjectID(dirName, opts['o'], opts['d'], 
		&newStack);
	
	if(id == 0)
	{
		BOX_ERROR("Directory '" << args[0] << "' not found.");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	
	// Store new stack
	mDirStack = newStack;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandChangeLocalDir(const std::vector<std::string> &)
//		Purpose: Change local directory command
//		Created: 2003/10/11
//
// --------------------------------------------------------------------------
void BackupQueries::CommandChangeLocalDir(const std::vector<std::string> &args)
{
	if(args.size() != 1 || args[0].size() == 0)
	{
		BOX_ERROR("Incorrect usage. lcd <local-directory>");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	
	// Try changing directory
#ifdef WIN32
	std::string dirName;
	if(!ConvertConsoleToUtf8(args[0].c_str(), dirName))
	{
		BOX_ERROR("Failed to convert path from console encoding.");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	int result = ::chdir(dirName.c_str());
#else
	int result = ::chdir(args[0].c_str());
#endif
	if(result != 0)
	{
		if(errno == ENOENT || errno == ENOTDIR)
		{
			BOX_ERROR("Directory '" << args[0] << "' does not exist.");
		}
		else
		{
			BOX_LOG_SYS_ERROR("Failed to change to directory "
				"'" << args[0] << "'");
		}

		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	
	// Report current dir
	char wd[PATH_MAX];
	if(::getcwd(wd, PATH_MAX) == 0)
	{
		BOX_LOG_SYS_ERROR("Error getting current directory");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}

#ifdef WIN32
	if(!ConvertUtf8ToConsole(wd, dirName))
	{
		BOX_ERROR("Failed to convert new path from console encoding.");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	BOX_INFO("Local current directory is now '" << dirName << "'.");
#else
	BOX_INFO("Local current directory is now '" << wd << "'.");
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandGetObject(const std::vector<std::string> &, const bool *)
//		Purpose: Gets an object without any translation.
//		Created: 2003/10/11
//
// --------------------------------------------------------------------------
void BackupQueries::CommandGetObject(const std::vector<std::string> &args, const bool *opts)
{
	// Check args
	if(args.size() != 2)
	{
		BOX_ERROR("Incorrect usage. getobject <object-id> "
			"<local-filename>");
		return;
	}
	
	int64_t id = ::strtoll(args[0].c_str(), 0, 16);
	if(id == std::numeric_limits<long long>::min() || id == std::numeric_limits<long long>::max() || id == 0)
	{
		BOX_ERROR("Not a valid object ID (specified in hex): " <<
			args[0]);
		return;
	}
	
	// Does file exist?
	EMU_STRUCT_STAT st;
	if(EMU_STAT(args[1].c_str(), &st) == 0 || errno != ENOENT)
	{
		BOX_ERROR("The local file '" << args[1] << " already exists.");
		return;
	}
	
	// Open file
	FileStream out(args[1].c_str(), O_WRONLY | O_CREAT | O_EXCL);
	
	// Request that object
	try
	{
		// Request object
		std::auto_ptr<BackupProtocolSuccess> getobj(mrConnection.QueryGetObject(id));

		// Stream that object out to the file
		std::auto_ptr<IOStream> objectStream(mrConnection.ReceiveStream());
		objectStream->CopyStreamTo(out);
			
		BOX_INFO("Object ID " << BOX_FORMAT_OBJECTID(id) <<
			" fetched successfully.");
	}
	catch(ConnectionException &e)
	{
		if(mrConnection.GetLastErrorType() == BackupProtocolError::Err_DoesNotExist)
		{
			BOX_ERROR("Object ID " << BOX_FORMAT_OBJECTID(id) <<
				" does not exist on store.");
			EMU_UNLINK(args[1].c_str());
		}
		else
		{
			BOX_ERROR("Error occured fetching object.");
			EMU_UNLINK(args[1].c_str());
		}
	}
	catch(...)
	{
		EMU_UNLINK(args[1].c_str());
		BOX_ERROR("Error occured fetching object.");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::FindFileID(const std::string&
//			 rNameOrIdString, const bool *options,
//			 int64_t *pDirIdOut, std::string* pFileNameOut)
//		Purpose: Locate a file on the store (either by name or by
//			 object ID, depending on opts['i'], where name can
//			 include a path) and return the file ID, placing the
//			 directory ID in *pDirIdOut and the filename part
//			 of the path in *pFileNameOut (if not NULL).
//		Created: 2008-09-12
//
// --------------------------------------------------------------------------
int64_t BackupQueries::FindFileID(const std::string& rNameOrIdString,
	const bool *opts, int64_t *pDirIdOut, std::string* pFileNameOut,
	int16_t flagsInclude, int16_t flagsExclude, int16_t* pFlagsOut)
{
	// Find object ID somehow
	int64_t fileId;
	int64_t dirId = GetCurrentDirectoryID();
	std::string fileName = rNameOrIdString;

	if(!opts['i'])
	{
		// does this remote filename include a path?
		std::string::size_type index = fileName.rfind('/');
		if(index != std::string::npos)
		{
			std::string dirName(fileName.substr(0, index));
			fileName = fileName.substr(index + 1);

			dirId = FindDirectoryObjectID(dirName);
			if(dirId == 0)
			{
				BOX_ERROR("Directory '" << dirName <<
					"' not found.");
				return 0;
			}
		}
	}

	BackupStoreFilenameClear fn(fileName);

	// Need to look it up in the current directory
	mrConnection.QueryListDirectory(
		dirId, flagsInclude, flagsExclude,
		true /* do want attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());
	BackupStoreDirectory::Entry *en;

	if(opts['i'])
	{
		// Specified as ID. 
		fileId = ::strtoll(rNameOrIdString.c_str(), 0, 16);
		if(fileId == std::numeric_limits<long long>::min() || 
			fileId == std::numeric_limits<long long>::max() || 
			fileId == 0)
		{
			BOX_ERROR("Not a valid object ID (specified in hex): "
				<< rNameOrIdString);
			return 0;
		}
		
		// Check that the item is actually in the directory
		en = dir.FindEntryByID(fileId);
		if(en == 0)
		{
			BOX_ERROR("File ID " << 
				BOX_FORMAT_OBJECTID(fileId) <<
				" not found in current directory on store.\n"
				"(You can only access files by ID from the "
				"current directory.)");
			return 0;
		}
	}
	else
	{				
		// Specified by name, find the object in the directory to get the ID
		BackupStoreDirectory::Iterator i(dir);
		en = i.FindMatchingClearName(fn);
		if(en == 0)
		{
			BOX_ERROR("Filename '" << rNameOrIdString << "' "
				"not found in current directory on store.\n"
				"(Subdirectories in path not searched.)");
			return 0;
		}
		
		fileId = en->GetObjectID();
	}

	*pDirIdOut = dirId;

	if(pFlagsOut)
	{
		*pFlagsOut = en->GetFlags();
	}

	if(pFileNameOut)
	{
		BackupStoreFilenameClear entryName(en->GetName());
		*pFileNameOut = entryName.GetClearFilename();
	}

	return fileId;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandGet(const std::vector<std::string> &, const bool *)
//		Purpose: Command to get a file from the store
//		Created: 2003/10/12
//
// --------------------------------------------------------------------------
void BackupQueries::CommandGet(std::vector<std::string> args, const bool *opts)
{
	// At least one argument?
	// Check args
	if(args.size() < 1 || (opts['i'] && args.size() != 2) || args.size() > 2)
	{
		BOX_ERROR("Incorrect usage.\n"
			"get <remote-filename> [<local-filename>] or\n"
			"get -i <object-id> <local-filename>");
		return;
	}

	// Find object ID somehow
	int64_t fileId, dirId;
	std::string localName;

#ifdef WIN32
	for (std::vector<std::string>::iterator 
		i = args.begin(); i != args.end(); i++)
	{
		std::string out;
		if(!ConvertConsoleToUtf8(i->c_str(), out))
		{
			BOX_ERROR("Failed to convert encoding.");
			return;
		}
		*i = out;
	}
#endif

	int16_t flagsExclude;

	if(opts['i'])
	{
		// can retrieve anything by ID
		flagsExclude = BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING;
	}
	else
	{
		// only current versions by name
		flagsExclude =
			BackupProtocolListDirectory::Flags_OldVersion |
			BackupProtocolListDirectory::Flags_Deleted;
	}


	fileId = FindFileID(args[0], opts, &dirId, &localName,
		BackupProtocolListDirectory::Flags_File, // just files
		flagsExclude, NULL /* don't care about flags found */);

	if (fileId == 0)
	{
		// error already reported
		return;
	}

	if(opts['i'])
	{
		// Specified as ID.  Must have a local name in the arguments
		// (check at beginning of function ensures this)
		localName = args[1];
	}
	else
	{				
		// Specified by name. Local name already set by FindFileID,
		// but may be overridden by user supplying a second argument.
		if(args.size() == 2)
		{
			localName = args[1];
		}
	}
	
	// Does local file already exist? (don't want to overwrite)
	EMU_STRUCT_STAT st;
	if(EMU_STAT(localName.c_str(), &st) == 0 || errno != ENOENT)
	{
		BOX_ERROR("The local file " << localName << " already exists, "
			"will not overwrite it.");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	
	// Request it from the store
	try
	{
		// Request object
		mrConnection.QueryGetFile(dirId, fileId);

		// Stream containing encoded file
		std::auto_ptr<IOStream> objectStream(mrConnection.ReceiveStream());
		
		// Decode it
		BackupStoreFile::DecodeFile(*objectStream, localName.c_str(), mrConnection.GetTimeout());

		// Done.
		BOX_INFO("Object ID " << BOX_FORMAT_OBJECTID(fileId) <<
			" fetched successfully.");
	}
	catch (BoxException &e)
	{
		BOX_ERROR("Failed to fetch file: " << 
			e.what());
		EMU_UNLINK(localName.c_str());
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to fetch file: " <<
			e.what());
		EMU_UNLINK(localName.c_str());
	}
	catch(...)
	{
		BOX_ERROR("Failed to fetch file: unknown error");
		EMU_UNLINK(localName.c_str());
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CompareParams::CompareParams()
//		Purpose: Constructor
//		Created: 29/1/04
//
// --------------------------------------------------------------------------
BackupQueries::CompareParams::CompareParams(bool QuickCompare,
	bool IgnoreExcludes, bool IgnoreAttributes,
	box_time_t LatestFileUploadTime)
: BoxBackupCompareParams(QuickCompare, IgnoreExcludes, IgnoreAttributes,
	LatestFileUploadTime),
  mDifferences(0),
  mDifferencesExplainedByModTime(0),
  mUncheckedFiles(0),
  mExcludedDirs(0),
  mExcludedFiles(0)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandCompare(const std::vector<std::string> &, const bool *)
//		Purpose: Command to compare data on the store with local data
//		Created: 2003/10/12
//
// --------------------------------------------------------------------------
void BackupQueries::CommandCompare(const std::vector<std::string> &args, const bool *opts)
{
	box_time_t LatestFileUploadTime = GetCurrentBoxTime();
	
	// Try and work out the time before which all files should be on the server
	{
		std::string syncTimeFilename(mrConfiguration.GetKeyValue("DataDirectory") + DIRECTORY_SEPARATOR_ASCHAR);
		syncTimeFilename += "last_sync_start";
		// Stat it to get file time
		EMU_STRUCT_STAT st;
		if(EMU_STAT(syncTimeFilename.c_str(), &st) == 0)
		{
			// Files modified after this time shouldn't be on the server, so report errors slightly differently
			LatestFileUploadTime = FileModificationTime(st) -
				SecondsToBoxTime(mrConfiguration.GetKeyValueInt("MinimumFileAge"));
		}
		else
		{
			BOX_WARNING("Failed to determine the time of the last "
				"synchronisation -- checks not performed.");
		}
	}

	// Parameters, including count of differences
	BackupQueries::CompareParams params(opts['q'], // quick compare?
		opts['E'], // ignore excludes
		opts['A'], // ignore attributes
		LatestFileUploadTime);
	
	params.mQuietCompare = opts['Q'];
	
	// Quick compare?
	if(params.QuickCompare())
	{
		BOX_WARNING("Quick compare used -- file attributes are not "
			"checked.");
	}
	
	if(!opts['l'] && opts['a'] && args.size() == 0)
	{
		// Compare all locations
		const Configuration &rLocations(
			mrConfiguration.GetSubConfiguration("BackupLocations"));
		std::vector<std::string> locNames =
			rLocations.GetSubConfigurationNames();
		for(std::vector<std::string>::iterator
			pLocName  = locNames.begin();
			pLocName != locNames.end();
			pLocName++)
		{
			CompareLocation(*pLocName, params);
		}
	}
	else if(opts['l'] && !opts['a'] && args.size() == 1)
	{
		// Compare one location
		CompareLocation(args[0], params);
	}
	else if(!opts['l'] && !opts['a'] && args.size() == 2)
	{
		// Compare directory to directory
		
		// Can't be bothered to do all the hard work to work out which location it's on, and hence which exclude list
		if(!params.IgnoreExcludes())
		{
			BOX_ERROR("Cannot use excludes on directory to directory comparison -- use -E flag to specify ignored excludes.");
			return;
		}
		else
		{
			// Do compare
			Compare(args[0], args[1], params);
		}
	}
	else
	{
		BOX_ERROR("Incorrect usage.\ncompare -a\n or compare -l <location-name>\n or compare <store-dir-name> <local-dir-name>");
		return;
	}

	if (!params.mQuietCompare)
	{	
		BOX_INFO("[ " <<
			params.mDifferencesExplainedByModTime << " (of " <<
			params.mDifferences << ") differences probably "
			"due to file modifications after the last upload ]");
	}

	BOX_INFO("Differences: " << params.mDifferences << " (" <<
		params.mExcludedDirs   << " dirs excluded, " <<
		params.mExcludedFiles  << " files excluded, " <<
		params.mUncheckedFiles << " files not checked)");
	
	// Set return code?
	if(opts['c'])
	{
		if (params.mUncheckedFiles != 0)
		{
			SetReturnCode(ReturnCode::Compare_Error);
		} 
		else if (params.mDifferences != 0)
		{
			SetReturnCode(ReturnCode::Compare_Different);
		}
		else
		{
			SetReturnCode(ReturnCode::Compare_Same);
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CompareLocation(const std::string &, BackupQueries::CompareParams &)
//		Purpose: Compare a location
//		Created: 2003/10/13
//
// --------------------------------------------------------------------------
void BackupQueries::CompareLocation(const std::string &rLocation,
	BoxBackupCompareParams &rParams)
{
	// Find the location's sub configuration
	const Configuration &locations(mrConfiguration.GetSubConfiguration("BackupLocations"));
	if(!locations.SubConfigurationExists(rLocation.c_str()))
	{
		BOX_ERROR("Location " << rLocation << " does not exist.");
		return;
	}
	const Configuration &loc(locations.GetSubConfiguration(rLocation.c_str()));

	#ifdef WIN32
	{
		std::string path = loc.GetKeyValue("Path");
		if (path.size() > 0 && path[path.size()-1] == 
			DIRECTORY_SEPARATOR_ASCHAR)
		{
			BOX_WARNING("Location '" << rLocation << "' path ends "
				"with '" DIRECTORY_SEPARATOR "', "
				"compare may fail!");
		}
	}
	#endif
	
	// Generate the exclude lists
	if(!rParams.IgnoreExcludes())
	{
		rParams.LoadExcludeLists(loc);
	}
			
	// Then get it compared
	Compare(std::string("/") + rLocation, loc.GetKeyValue("Path"), rParams);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::Compare(const std::string &,
//			 const std::string &, BackupQueries::CompareParams &)
//		Purpose: Compare a store directory against a local directory
//		Created: 2003/10/13
//
// --------------------------------------------------------------------------
void BackupQueries::Compare(const std::string &rStoreDir,
	const std::string &rLocalDir, BoxBackupCompareParams &rParams)
{
#ifdef WIN32
	std::string localDirEncoded;
	std::string storeDirEncoded;
	if(!ConvertConsoleToUtf8(rLocalDir.c_str(), localDirEncoded)) return;
	if(!ConvertConsoleToUtf8(rStoreDir.c_str(), storeDirEncoded)) return;
#else
	const std::string& localDirEncoded(rLocalDir);
	const std::string& storeDirEncoded(rStoreDir);
#endif
	
	// Get the directory ID of the directory -- only use current data
	int64_t dirID = FindDirectoryObjectID(storeDirEncoded);
	
	// Found?
	if(dirID == 0)
	{
		bool modifiedAfterLastSync = false;
		
		EMU_STRUCT_STAT st;
		if(EMU_STAT(rLocalDir.c_str(), &st) == 0)
		{
			if(FileAttrModificationTime(st) >
				rParams.LatestFileUploadTime())
			{
				modifiedAfterLastSync = true;
			}
		}
		
		rParams.NotifyRemoteFileMissing(localDirEncoded,
			storeDirEncoded, modifiedAfterLastSync);
		return;
	}
	
	// Go!
	Compare(dirID, storeDirEncoded, localDirEncoded, rParams);
}

void BackupQueries::CompareOneFile(int64_t DirID,
	BackupStoreDirectory::Entry *pEntry,
	const std::string& rLocalPath,
	const std::string& rStorePath,
	BoxBackupCompareParams &rParams)
{
	int64_t fileId = pEntry->GetObjectID();
	int64_t fileSize = 0;

	EMU_STRUCT_STAT st;
	if(EMU_STAT(rLocalPath.c_str(), &st) == 0)
	{
		fileSize = st.st_size;
	}

	try
	{
		// Files the same flag?
		bool equal = true;
		
		// File modified after last sync flag
		bool modifiedAfterLastSync = false;
		
		bool hasDifferentAttribs = false;

		bool alreadyReported = false;
			
		if(rParams.QuickCompare())
		{
			// Compare file -- fetch it
			mrConnection.QueryGetBlockIndexByID(fileId);

			// Stream containing block index
			std::auto_ptr<IOStream> blockIndexStream(mrConnection.ReceiveStream());
			
			// Compare
			equal = BackupStoreFile::CompareFileContentsAgainstBlockIndex(
				rLocalPath.c_str(), *blockIndexStream,
				mrConnection.GetTimeout());
		}
		else
		{
			// Compare file -- fetch it
			mrConnection.QueryGetFile(DirID, pEntry->GetObjectID());

			// Stream containing encoded file
			std::auto_ptr<IOStream> objectStream(mrConnection.ReceiveStream());

			// Decode it
			std::auto_ptr<BackupStoreFile::DecodedStream> fileOnServerStream;

			// Got additional attributes?
			if(pEntry->HasAttributes())
			{
				// Use these attributes
				const StreamableMemBlock &storeAttr(pEntry->GetAttributes());
				BackupClientFileAttributes attr(storeAttr);
				fileOnServerStream.reset(
					BackupStoreFile::DecodeFileStream(
						*objectStream,
						mrConnection.GetTimeout(),
						&attr).release());
			}
			else
			{
				// Use attributes stored in file
				fileOnServerStream.reset(BackupStoreFile::DecodeFileStream(*objectStream, mrConnection.GetTimeout()).release());
			}
			
			// Should always be something in the auto_ptr, it's how the interface is defined. But be paranoid.
			if(!fileOnServerStream.get())
			{
				THROW_EXCEPTION(BackupStoreException, Internal)
			}
			
			// Compare attributes
			BackupClientFileAttributes localAttr;
			box_time_t fileModTime = 0;
			localAttr.ReadAttributes(rLocalPath.c_str(), false /* don't zero mod times */, &fileModTime);					
			modifiedAfterLastSync = (fileModTime > rParams.LatestFileUploadTime());
			bool ignoreAttrModTime = true;

			#ifdef WIN32
			// attr mod time is really
			// creation time, so check it
			ignoreAttrModTime = false;
			#endif

			if(!rParams.IgnoreAttributes() &&
			#ifdef PLATFORM_DISABLE_SYMLINK_ATTRIB_COMPARE
			   !fileOnServerStream->IsSymLink() &&
			#endif
			   !localAttr.Compare(fileOnServerStream->GetAttributes(),
					ignoreAttrModTime,
					fileOnServerStream->IsSymLink() /* ignore modification time if it's a symlink */))
			{
				hasDifferentAttribs = true;
			}

			// Compare contents, if it's a regular file not a link
			// Remember, we MUST read the entire stream from the server.
			SelfFlushingStream flushObject(*objectStream);

			if(!fileOnServerStream->IsSymLink())
			{
				SelfFlushingStream flushFile(*fileOnServerStream);
				// Open the local file
				std::auto_ptr<FileStream> apLocalFile;

				try
				{
					apLocalFile.reset(new FileStream(rLocalPath.c_str()));
				}
				catch(std::exception &e)
				{
					rParams.NotifyLocalFileReadFailed(rLocalPath,
						rStorePath, fileSize, e);
					alreadyReported = true;
				}
				catch(...)
				{	
					rParams.NotifyLocalFileReadFailed(rLocalPath,
						rStorePath, fileSize);
					alreadyReported = true;
				}

				if(apLocalFile.get())
				{
					equal = apLocalFile->CompareWith(*fileOnServerStream,
						mrConnection.GetTimeout());
				}
			}
		}

		rParams.NotifyFileCompared(rLocalPath, rStorePath, fileSize,
			hasDifferentAttribs, !equal, modifiedAfterLastSync,
			pEntry->HasAttributes());
	}
	catch(BoxException &e)
	{
		rParams.NotifyDownloadFailed(rLocalPath, rStorePath, fileSize,
			e);
	}
	catch(std::exception &e)
	{
		rParams.NotifyDownloadFailed(rLocalPath, rStorePath, fileSize,
			e);
	}
	catch(...)
	{	
		rParams.NotifyDownloadFailed(rLocalPath, rStorePath, fileSize);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::Compare(int64_t, const std::string &,
//			 const std::string &, BackupQueries::CompareParams &)
//		Purpose: Compare a store directory against a local directory
//		Created: 2003/10/13
//
// --------------------------------------------------------------------------
void BackupQueries::Compare(int64_t DirID, const std::string &rStoreDir,
	const std::string &rLocalDir, BoxBackupCompareParams &rParams)
{
	rParams.NotifyDirComparing(rLocalDir, rStoreDir);

	// Get info on the local directory
	EMU_STRUCT_STAT st;
	if(EMU_LSTAT(rLocalDir.c_str(), &st) != 0)
	{
		// What kind of error?
		if(errno == ENOTDIR || errno == ENOENT)
		{
			rParams.NotifyLocalDirMissing(rLocalDir, rStoreDir);
		}
		else
		{
			rParams.NotifyLocalDirAccessFailed(rLocalDir, rStoreDir);
		}
		return;
	}

	// Get the directory listing from the store
	mrConnection.QueryListDirectory(
		DirID,
		BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
		// get everything
		BackupProtocolListDirectory::Flags_OldVersion |
		BackupProtocolListDirectory::Flags_Deleted,
		// except for old versions and deleted files
		true /* want attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

	// Test out the attributes
	if(!dir.HasAttributes())
	{
		rParams.NotifyStoreDirMissingAttributes(rLocalDir, rStoreDir);
	}
	else
	{
		// Fetch the attributes
		const StreamableMemBlock &storeAttr(dir.GetAttributes());
		BackupClientFileAttributes attr(storeAttr);

		// Get attributes of local directory
		BackupClientFileAttributes localAttr;
		localAttr.ReadAttributes(rLocalDir.c_str(), 
			true /* directories have zero mod times */);

		if(attr.Compare(localAttr, true, true /* ignore modification times */))
		{
			rParams.NotifyDirCompared(rLocalDir, rStoreDir,
				false, false /* actually we didn't check :) */);
		}
		else
		{
			bool modifiedAfterLastSync = false;
			
			EMU_STRUCT_STAT st;
			if(EMU_STAT(rLocalDir.c_str(), &st) == 0)
			{
				if(FileAttrModificationTime(st) >
					rParams.LatestFileUploadTime())
				{
					modifiedAfterLastSync = true;
				}
			}
			
			rParams.NotifyDirCompared(rLocalDir, rStoreDir,
				true, modifiedAfterLastSync);
		}
	}

	// Open the local directory
	DIR *dirhandle = ::opendir(rLocalDir.c_str());
	if(dirhandle == 0)
	{
		rParams.NotifyLocalDirAccessFailed(rLocalDir, rStoreDir);
		return;
	}
	
	try
	{
		// Read the files and directories into sets
		std::set<std::string> localFiles;
		std::set<std::string> localDirs;
		struct dirent *localDirEn = 0;
		while((localDirEn = readdir(dirhandle)) != 0)
		{
			// Not . and ..!
			if(localDirEn->d_name[0] == '.' && 
				(localDirEn->d_name[1] == '\0' || (localDirEn->d_name[1] == '.' && localDirEn->d_name[2] == '\0')))
			{
				// ignore, it's . or ..
				
#ifdef HAVE_VALID_DIRENT_D_TYPE
				if (localDirEn->d_type != DT_DIR)
				{
					BOX_ERROR("d_type does not really "
						"work on your platform. "
						"Reconfigure Box!");
					return;
				}
#endif
				
				continue;
			}

			std::string localDirPath(MakeFullPath(rLocalDir,
				localDirEn->d_name));
			std::string storeDirPath(rStoreDir + "/" +
				localDirEn->d_name);

#ifndef HAVE_VALID_DIRENT_D_TYPE
			EMU_STRUCT_STAT st;
			if(EMU_LSTAT(localDirPath.c_str(), &st) != 0)
			{
				// Check whether dir is excluded before trying
				// to stat it, to fix problems with .gvfs
				// directories that are not readable by root
				// causing compare to crash:
				// http://lists.boxbackup.org/pipermail/boxbackup/2010-January/000013.html
				if(rParams.IsExcludedDir(localDirPath))
				{
					rParams.NotifyExcludedDir(localDirPath,
						storeDirPath);
					continue;
				}
				else
				{
					THROW_EXCEPTION_MESSAGE(CommonException,
						OSFileError, localDirPath);
				}
			}
			
			// Entry -- file or dir?
			if(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
			{	
			    // File or symbolic link
			    localFiles.insert(std::string(localDirEn->d_name));
			}
			else if(S_ISDIR(st.st_mode))
			{
			    // Directory
			    localDirs.insert(std::string(localDirEn->d_name));
			}
#else
			// Entry -- file or dir?
			if(localDirEn->d_type == DT_REG || localDirEn->d_type == DT_LNK)
			{
				// File or symbolic link
				localFiles.insert(std::string(localDirEn->d_name));
			}
			else if(localDirEn->d_type == DT_DIR)
			{
				// Directory
				localDirs.insert(std::string(localDirEn->d_name));
			}
#endif
		}
		// Close directory
		if(::closedir(dirhandle) != 0)
		{
			BOX_LOG_SYS_ERROR("Failed to close local directory "
				"'" << rLocalDir << "'");
		}
		dirhandle = 0;
	
		// Do the same for the store directories
		std::set<std::pair<std::string, BackupStoreDirectory::Entry *> > storeFiles;
		std::set<std::pair<std::string, BackupStoreDirectory::Entry *> > storeDirs;
		
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *storeDirEn = 0;
		while((storeDirEn = i.Next()) != 0)
		{
			// Decrypt filename
			BackupStoreFilenameClear name(storeDirEn->GetName());
		
			// What is it?
			if((storeDirEn->GetFlags() & BackupStoreDirectory::Entry::Flags_File) == BackupStoreDirectory::Entry::Flags_File)
			{
				// File
				storeFiles.insert(std::pair<std::string, BackupStoreDirectory::Entry *>(name.GetClearFilename(), storeDirEn));
			}
			else
			{
				// Dir
				storeDirs.insert(std::pair<std::string, BackupStoreDirectory::Entry *>(name.GetClearFilename(), storeDirEn));
			}
		}

#ifdef _MSC_VER
		typedef std::set<std::string>::iterator string_set_iter_t;
#else
		typedef std::set<std::string>::const_iterator string_set_iter_t;
#endif
		
		// Now compare files.
		for(std::set<std::pair<std::string, BackupStoreDirectory::Entry *> >::const_iterator i = storeFiles.begin(); i != storeFiles.end(); ++i)
		{
			const std::string& fileName(i->first);

			std::string localPath(MakeFullPath(rLocalDir, fileName));
			std::string storePath(rStoreDir + "/" + fileName);

			rParams.NotifyFileComparing(localPath, storePath);
			
			// Does the file exist locally?
			string_set_iter_t local(localFiles.find(fileName));
			if(local == localFiles.end())
			{
				// Not found -- report
				rParams.NotifyLocalFileMissing(localPath,
					storePath);
			}
			else
			{				
				CompareOneFile(DirID, i->second, localPath,
					storePath, rParams);

				// Remove from set so that we know it's been compared
				localFiles.erase(local);
			}
		}
		
		// Report any files which exist locally, but not on the store
		for(string_set_iter_t i = localFiles.begin(); i != localFiles.end(); ++i)
		{
			std::string localPath(MakeFullPath(rLocalDir, *i));
			std::string storePath(rStoreDir + "/" + *i);

			// Should this be ignored (ie is excluded)?
			if(!rParams.IsExcludedFile(localPath))
			{
				bool modifiedAfterLastSync = false;
				
				EMU_STRUCT_STAT st;
				if(EMU_STAT(localPath.c_str(), &st) == 0)
				{
					if(FileModificationTime(st) >
						rParams.LatestFileUploadTime())
					{
						modifiedAfterLastSync = true;
					}
				}
				
				rParams.NotifyRemoteFileMissing(localPath,
					storePath, modifiedAfterLastSync);
			}
			else
			{
				rParams.NotifyExcludedFile(localPath,
					storePath);
			}
		}		
		
		// Finished with the files, clear the sets to reduce memory usage slightly
		localFiles.clear();
		storeFiles.clear();
		
		// Now do the directories, recursively to check subdirectories
		for(std::set<std::pair<std::string, BackupStoreDirectory::Entry *> >::const_iterator i = storeDirs.begin(); i != storeDirs.end(); ++i)
		{
			std::string localPath(MakeFullPath(rLocalDir, i->first));
			std::string storePath(rStoreDir + "/" + i->first);

			// Does the directory exist locally?
			string_set_iter_t local(localDirs.find(i->first));
			if(local == localDirs.end() &&
				rParams.IsExcludedDir(localPath))
			{
				rParams.NotifyExcludedFileNotDeleted(localPath,
					storePath);
			}
			else if(local == localDirs.end())
			{
				// Not found -- report
				rParams.NotifyLocalFileMissing(localPath,
					storePath);
			}
			else if(rParams.IsExcludedDir(localPath))
			{
				// don't recurse into excluded directories
			}
			else
			{
				// Compare directory
				Compare(i->second->GetObjectID(),
					storePath, localPath, rParams);
				
				// Remove from set so that we know it's been compared
				localDirs.erase(local);
			}
		}
		
		// Report any directories which exist locally, but not on the store
		for(std::set<std::string>::const_iterator
			i  = localDirs.begin();
			i != localDirs.end(); ++i)
		{
			std::string localPath(MakeFullPath(rLocalDir, *i));
			std::string storePath(rStoreDir + "/" + *i);

			// Should this be ignored (ie is excluded)?
			if(!rParams.IsExcludedDir(localPath))
			{
				bool modifiedAfterLastSync = false;
				
				// Check the dir modification time
				EMU_STRUCT_STAT st;
				if(EMU_STAT(localPath.c_str(), &st) == 0 &&
					FileModificationTime(st) >
					rParams.LatestFileUploadTime())
				{
					modifiedAfterLastSync = true;
				}

				rParams.NotifyRemoteFileMissing(localPath,
					storePath, modifiedAfterLastSync);
			}
			else
			{
				rParams.NotifyExcludedDir(localPath, storePath);
			}
		}		
	}
	catch(...)
	{
		if(dirhandle != 0)
		{
			::closedir(dirhandle);
		}
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandRestore(const std::vector<std::string> &, const bool *)
//		Purpose: Restore a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
void BackupQueries::CommandRestore(const std::vector<std::string> &args, const bool *opts)
{
	// Check arguments
	if(args.size() < 1 || args.size() > 2)
	{
		BOX_ERROR("Incorrect usage. restore [-drif] <remote-name> "
			"[<local-name>]");
		return;
	}

	// Restoring deleted things?
	bool restoreDeleted = opts['d'];

	std::string storeDirEncoded;

	// Get directory ID
	int64_t dirID = 0;
	if(opts['i'])
	{
		// Specified as ID. 
		dirID = ::strtoll(args[0].c_str(), 0, 16);
		if(dirID == std::numeric_limits<long long>::min() || dirID == std::numeric_limits<long long>::max() || dirID == 0)
		{
			BOX_ERROR("Not a valid object ID (specified in hex): "
				<< args[0]);
			return;
		}
		std::ostringstream oss;
		oss << BOX_FORMAT_OBJECTID(args[0]);
		storeDirEncoded = oss.str();
	}
	else
	{
#ifdef WIN32
		if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded))
			return;
#else
		storeDirEncoded = args[0];
#endif
	
		// Look up directory ID
		dirID = FindDirectoryObjectID(storeDirEncoded, 
			false /* no old versions */, 
			restoreDeleted /* find deleted dirs */);
	}
	
	// Allowable?
	if(dirID == 0)
	{
		BOX_ERROR("Directory '" << args[0] << "' not found on server");
		return;
	}

	if(dirID == BackupProtocolListDirectory::RootDirectory)
	{
		BOX_ERROR("Cannot restore the root directory -- restore locations individually.");
		return;
	}

	std::string localName;

	if(args.size() == 2)
	{
		#ifdef WIN32
			if(!ConvertConsoleToUtf8(args[1].c_str(), localName))
			{
				return;
			}
		#else
			localName = args[1];
		#endif
	}
	else
	{
		localName = args[0];
	}

	// Go and restore...
	int result;

	try
	{
		// At TRACE level, we print a line for each file and
		// directory, so we don't need dots.

		result = BackupClientRestore(mrConnection, dirID, 
			storeDirEncoded.c_str(), localName.c_str(), 
			true /* print progress dots */, restoreDeleted, 
			false /* don't undelete after restore! */, 
			opts['r'] /* resume? */,
			opts['f'] /* force continue after errors */);
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to restore: " << e.what());
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}
	catch(...)
	{
		BOX_ERROR("Failed to restore: unknown exception");
		SetReturnCode(ReturnCode::Command_Error);
		return;
	}

	switch(result)
	{
	case Restore_Complete:
		BOX_INFO("Restore complete.");
		break;
	
	case Restore_CompleteWithErrors:
		BOX_WARNING("Restore complete, but some files could not be "
			"restored.");
		break;
	
	case Restore_ResumePossible:
		BOX_ERROR("Resume possible -- repeat command with -r flag "
			"to resume.");
		SetReturnCode(ReturnCode::Command_Error);
		break;
	
	case Restore_TargetExists:
		BOX_ERROR("The target directory exists. You cannot restore "
			"over an existing directory.");
		SetReturnCode(ReturnCode::Command_Error);
		break;
		
	case Restore_TargetPathNotFound:
		BOX_ERROR("The target directory path does not exist.\n"
			"To restore to a directory whose parent "
			"does not exist, create the parent first.");
		SetReturnCode(ReturnCode::Command_Error);
		break;

	case Restore_UnknownError:
		BOX_ERROR("Unknown error during restore.");
		SetReturnCode(ReturnCode::Command_Error);
		break;

	default:
		BOX_ERROR("Unknown restore result " << result << ".");
		SetReturnCode(ReturnCode::Command_Error);
		break;
	}
}



// These are autogenerated by a script.
extern const char *help_commands[];
extern const char *help_text[];


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandHelp(const std::vector<std::string> &args)
//		Purpose: Display help on commands
//		Created: 15/2/04
//
// --------------------------------------------------------------------------
void BackupQueries::CommandHelp(const std::vector<std::string> &args)
{
	if(args.size() == 0)
	{
		// Display a list of all commands
		printf("Available commands are:\n");
		for(int c = 0; help_commands[c] != 0; ++c)
		{
			printf("    %s\n", help_commands[c]);
		}
		printf("Type \"help <command>\" for more information on a command.\n\n");
	}
	else
	{
		// Display help on a particular command
		int c;
		for(c = 0; help_commands[c] != 0; ++c)
		{
			if(::strcmp(help_commands[c], args[0].c_str()) == 0)
			{
				// Found the command, print help
				printf("\n%s\n", help_text[c]);
				break;
			}
		}
		if(help_commands[c] == 0)
		{
			printf("No help found for command '%s'\n", args[0].c_str());
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandUsage()
//		Purpose: Display storage space used on server
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
void BackupQueries::CommandUsage(const bool *opts)
{
	bool MachineReadable = opts['m'];

	// Request full details from the server
	std::auto_ptr<BackupProtocolAccountUsage> usage(mrConnection.QueryGetAccountUsage());

	// Display each entry in turn
	int64_t hardLimit = usage->GetBlocksHardLimit();
	int32_t blockSize = usage->GetBlockSize();
	CommandUsageDisplayEntry("Used", usage->GetBlocksUsed(), hardLimit,
		blockSize, MachineReadable);
	CommandUsageDisplayEntry("Old files", usage->GetBlocksInOldFiles(),
		hardLimit, blockSize, MachineReadable);
	CommandUsageDisplayEntry("Deleted files", usage->GetBlocksInDeletedFiles(),
		hardLimit, blockSize, MachineReadable);
	CommandUsageDisplayEntry("Directories", usage->GetBlocksInDirectories(),
		hardLimit, blockSize, MachineReadable);
	CommandUsageDisplayEntry("Soft limit", usage->GetBlocksSoftLimit(),
		hardLimit, blockSize, MachineReadable);
	CommandUsageDisplayEntry("Hard limit", hardLimit, hardLimit, blockSize,
		MachineReadable);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandUsageDisplayEntry(const char *,
//			 int64_t, int64_t, int32_t, bool)
//		Purpose: Display an entry in the usage table
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
void BackupQueries::CommandUsageDisplayEntry(const char *Name, int64_t Size,
int64_t HardLimit, int32_t BlockSize, bool MachineReadable)
{
	std::cout << FormatUsageLineStart(Name, MachineReadable) <<
		FormatUsageBar(Size, Size * BlockSize, HardLimit * BlockSize,
			MachineReadable) << std::endl;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandUndelete(const std::vector<std::string> &, const bool *)
//		Purpose: Undelete a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
void BackupQueries::CommandUndelete(const std::vector<std::string> &args, const bool *opts)
{
	if (!mReadWrite)
	{
		BOX_ERROR("This command requires a read-write connection. "
			"Please reconnect with the -w option.");
		return;
	}

	// Check arguments
	if(args.size() != 1)
	{
		BOX_ERROR("Incorrect usage. undelete <name> or undelete -i <object-id>");
		return;
	}

#ifdef WIN32
	std::string storeDirEncoded;
	if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded)) return;
#else
	const std::string& storeDirEncoded(args[0]);
#endif

	// Find object ID somehow
	int64_t fileId, parentId;
	std::string fileName;
	int16_t flagsOut;

	fileId = FindFileID(storeDirEncoded, opts, &parentId, &fileName,
		/* include files and directories */
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		/* include old and deleted files */
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		&flagsOut);

	if (fileId == 0)
	{
		// error already reported
		return;
	}

	// Undelete it on the store
	try
	{
		// Undelete object
		if(flagsOut & BackupProtocolListDirectory::Flags_File)
		{
			mrConnection.QueryUndeleteFile(parentId, fileId);
		}
		else
		{
			mrConnection.QueryUndeleteDirectory(fileId);
		}
	}
	catch (BoxException &e)
	{
		BOX_ERROR("Failed to undelete object: " << 
			e.what());
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to undelete object: " <<
			e.what());
	}
	catch(...)
	{
		BOX_ERROR("Failed to undelete object: unknown error");
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandDelete(const
//			 std::vector<std::string> &, const bool *)
//		Purpose: Deletes a file
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
void BackupQueries::CommandDelete(const std::vector<std::string> &args,
	const bool *opts)
{
	if (!mReadWrite)
	{
		BOX_ERROR("This command requires a read-write connection. "
			"Please reconnect with the -w option.");
		return;
	}

	// Check arguments
	if(args.size() != 1)
	{
		BOX_ERROR("Incorrect usage. delete <name>");
		return;
	}

#ifdef WIN32
	std::string storeDirEncoded;
	if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded)) return;
#else
	const std::string& storeDirEncoded(args[0]);
#endif

	// Find object ID somehow
	int64_t fileId, parentId;
	std::string fileName;
	int16_t flagsOut;

	fileId = FindFileID(storeDirEncoded, opts, &parentId, &fileName,
		/* include files and directories */
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		/* exclude old and deleted files */
		BackupProtocolListDirectory::Flags_OldVersion |
		BackupProtocolListDirectory::Flags_Deleted,
		&flagsOut);

	if (fileId == 0)
	{
		// error already reported
		return;
	}

	BackupStoreFilenameClear fn(fileName);

	// Delete it on the store
	try
	{
		// Delete object
		if(flagsOut & BackupProtocolListDirectory::Flags_File)
		{
			mrConnection.QueryDeleteFile(parentId, fn);
		}
		else
		{
			mrConnection.QueryDeleteDirectory(fileId);
		}
	}
	catch (BoxException &e)
	{
		BOX_ERROR("Failed to delete object: " << 
			e.what());
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to delete object: " <<
			e.what());
	}
	catch(...)
	{
		BOX_ERROR("Failed to delete object: unknown error");
	}
}
