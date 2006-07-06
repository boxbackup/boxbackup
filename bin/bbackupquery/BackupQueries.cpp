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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_DIRENT_H
	#include <dirent.h>
#endif

#include <set>

#include "BackupQueries.h"
#include "Utils.h"
#include "Configuration.h"
#include "autogen_BackupProtocolClient.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreDirectory.h"
#include "IOStream.h"
#include "BoxTimeToText.h"
#include "FileStream.h"
#include "BackupStoreFile.h"
#include "TemporaryDirectory.h"
#include "FileModificationTime.h"
#include "BackupClientFileAttributes.h"
#include "CommonException.h"
#include "BackupClientRestore.h"
#include "BackupStoreException.h"
#include "ExcludeList.h"
#include "BackupClientMakeExcludeList.h"

#include "MemLeakFindOn.h"

#define COMPARE_RETURN_SAME			1
#define COMPARE_RETURN_DIFFERENT	2
#define COMPARE_RETURN_ERROR		3


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::BackupQueries()
//		Purpose: Constructor
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
BackupQueries::BackupQueries(BackupProtocolClient &rConnection, const Configuration &rConfiguration)
	: mrConnection(rConnection),
	  mrConfiguration(rConfiguration),
	  mQuitNow(false),
	  mRunningAsRoot(false),
	  mWarnedAboutOwnerAttributes(false),
	  mReturnCode(0)		// default return code
{
	mRunningAsRoot = (::geteuid() == 0);
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

typedef struct cmd_info
{
	const char* name;
	const char* opts;
} cmd_info_t;

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::DoCommand(const char *)
//		Purpose: Perform a command
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::DoCommand(const char *Command)
{
	// is the command a shell command?
	if(Command[0] == 's' && Command[1] == 'h' && Command[2] == ' ' && Command[3] != '\0')
	{
		// Yes, run shell command
		::system(Command + 3);
		return;
	}

	// split command into components
	std::vector<std::string> cmdElements;
	std::string options;
	{
		const char *c = Command;
		bool inQuoted = false;
		bool inOptions = false;
		
		std::string s;
		while(*c != 0)
		{
			// Terminating char?
			if(*c == ((inQuoted)?'"':' '))
			{
				if(!s.empty()) cmdElements.push_back(s);
				s.resize(0);
				inQuoted = false;
				inOptions = false;
			}
			else
			{
				// No. Start of quoted parameter?
				if(s.empty() && *c == '"')
				{
					inQuoted = true;
				}
				// Start of options?
				else if(s.empty() && *c == '-')
				{
					inOptions = true;
				}
				else
				{
					if(inOptions)
					{
						// Option char
						options += *c;
					}
					else
					{
						// Normal string char
						s += *c;
					}
				}
			}
		
			++c;
		}
		if(!s.empty()) cmdElements.push_back(s);
	}
	
	// Check...
	if(cmdElements.size() < 1)
	{
		// blank command
		return;
	}
	
	// Data about commands
	static cmd_info_t commands[] = 
	{
		{ "quit", "" },
		{ "exit", "" },
		{ "list", "rodIFtTsh", },
		{ "pwd",  "" },
		{ "cd",   "od" },
		{ "lcd",  "" },
		{ "sh",   "" },
		{ "getobject", "" },
		{ "get",  "i" },
		{ "compare", "alcqAE" },
		{ "restore", "dri" },
		{ "help", "" },
		{ "usage", "" },
		{ "undelete", "" },
		{ NULL, NULL } 
	};
	#define COMMAND_Quit		0
	#define COMMAND_Exit		1
	#define COMMAND_List		2
	#define COMMAND_pwd			3
	#define COMMAND_cd			4
	#define COMMAND_lcd			5
	#define COMMAND_sh			6
	#define COMMAND_GetObject	7
	#define COMMAND_Get			8
	#define COMMAND_Compare		9
	#define COMMAND_Restore		10
	#define COMMAND_Help		11
	#define COMMAND_Usage		12
	#define COMMAND_Undelete	13
	static const char *alias[] = {"ls",			0};
	static const int aliasIs[] = {COMMAND_List, 0};
	
	// Work out which command it is...
	int cmd = 0;
	while(commands[cmd].name != 0 && ::strcmp(cmdElements[0].c_str(), commands[cmd].name) != 0)
	{
		cmd++;
	}
	if(commands[cmd].name == 0)
	{
		// Check for aliases
		int a;
		for(a = 0; alias[a] != 0; ++a)
		{
			if(::strcmp(cmdElements[0].c_str(), alias[a]) == 0)
			{
				// Found an alias
				cmd = aliasIs[a];
				break;
			}
		}
	
		// No such command
		if(alias[a] == 0)
		{
			printf("Unrecognised command: %s\n", Command);
			return;
		}
	}

	// Arguments
	std::vector<std::string> args(cmdElements.begin() + 1, cmdElements.end());

	// Set up options
	bool opts[256];
	for(int o = 0; o < 256; ++o) opts[o] = false;
	// BLOCK
	{
		// options
		const char *c = options.c_str();
		while(*c != 0)
		{
			// Valid option?
			if(::strchr(commands[cmd].opts, *c) == NULL)
			{
				printf("Invalid option '%c' for command %s\n", 
					*c, commands[cmd].name);
				return;
			}
			opts[(int)*c] = true;
			++c;
		}
	}

	if(cmd != COMMAND_Quit && cmd != COMMAND_Exit)
	{
		// If not a quit command, set the return code to zero
		SetReturnCode(0);
	}

	// Handle command
	switch(cmd)
	{
	case COMMAND_Quit:
	case COMMAND_Exit:
		mQuitNow = true;
		break;
		
	case COMMAND_List:
		CommandList(args, opts);
		break;
		
	case COMMAND_pwd:
		{
			// Simple implementation, so do it here
			printf("%s (%08llx)\n", 
				GetCurrentDirectoryName().c_str(), 
				(long long)GetCurrentDirectoryID());
		}
		break;

	case COMMAND_cd:
		CommandChangeDir(args, opts);
		break;
		
	case COMMAND_lcd:
		CommandChangeLocalDir(args);
		break;
		
	case COMMAND_sh:
		printf("The command to run must be specified as an argument.\n");
		break;
		
	case COMMAND_GetObject:
		CommandGetObject(args, opts);
		break;
		
	case COMMAND_Get:
		CommandGet(args, opts);
		break;
		
	case COMMAND_Compare:
		CommandCompare(args, opts);
		break;
		
	case COMMAND_Restore:
		CommandRestore(args, opts);
		break;
		
	case COMMAND_Usage:
		CommandUsage();
		break;
		
	case COMMAND_Help:
		CommandHelp(args);
		break;

	case COMMAND_Undelete:
		CommandUndelete(args, opts);
		break;
		
	default:
		break;
	}
}


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
	#define LIST_OPTION_RECURSIVE		'r'
	#define LIST_OPTION_ALLOWOLD		'o'
	#define LIST_OPTION_ALLOWDELETED	'd'
	#define LIST_OPTION_NOOBJECTID		'I'
	#define LIST_OPTION_NOFLAGS		'F'
	#define LIST_OPTION_TIMES_LOCAL		't'
	#define LIST_OPTION_TIMES_UTC		'T'
	#define LIST_OPTION_SIZEINBLOCKS	's'
	#define LIST_OPTION_DISPLAY_HASH	'h'

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
			printf("Directory '%s' not found on store\n",
				args[0].c_str());
			return;
		}
	}
	
	// List it
	List(rootDir, listRoot, opts, true /* first level to list */);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::List(int64_t, const std::string &, const bool *, bool)
//		Purpose: Do the actual listing of directories and files
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
void BackupQueries::List(int64_t DirID, const std::string &rListRoot, const bool *opts, bool FirstLevel)
{
	// Generate exclude flags
	int16_t excludeFlags = BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING;
	if(!opts[LIST_OPTION_ALLOWOLD]) excludeFlags |= BackupProtocolClientListDirectory::Flags_OldVersion;
	if(!opts[LIST_OPTION_ALLOWDELETED]) excludeFlags |= BackupProtocolClientListDirectory::Flags_Deleted;

	// Do communication
	mrConnection.QueryListDirectory(
			DirID,
			BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,	// both files and directories
			excludeFlags,
			true /* want attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

	// Then... display everything
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next()) != 0)
	{
		// Display this entry
		BackupStoreFilenameClear clear(en->GetName());
		
		// Object ID?
		if(!opts[LIST_OPTION_NOOBJECTID])
		{
			// add object ID to line
#ifdef _MSC_VER
			printf("%08I64x ", (int64_t)en->GetObjectID());
#else
			printf("%08llx ", (long long)en->GetObjectID());
#endif
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
			printf(displayflags);
			
			if(en_flags != 0)
			{
				printf("[ERROR: Entry has additional flags set] ");
			}
		}
		
		if(opts[LIST_OPTION_TIMES_UTC])
		{
			// Show UTC times...
			std::string time = BoxTimeToISO8601String(
				en->GetModificationTime(), false);
			printf("%s ", time.c_str());
		}

		if(opts[LIST_OPTION_TIMES_LOCAL])
		{
			// Show local times...
			std::string time = BoxTimeToISO8601String(
				en->GetModificationTime(), true);
			printf("%s ", time.c_str());
		}
		
		if(opts[LIST_OPTION_DISPLAY_HASH])
		{
#ifdef _MSC_VER
			printf("%016I64x ", (int64_t)en->GetAttributesHash());
#else
			printf("%016llx ", (long long)en->GetAttributesHash());
#endif
		}
		
		if(opts[LIST_OPTION_SIZEINBLOCKS])
		{
#ifdef _MSC_VER
			printf("%05I64d ", (int64_t)en->GetSizeInBlocks());
#else
			printf("%05lld ", (long long)en->GetSizeInBlocks());
#endif
		}
		
		// add name
		if(!FirstLevel)
		{
#ifdef WIN32
			std::string listRootDecoded;
			if(!ConvertUtf8ToConsole(rListRoot.c_str(), 
				listRootDecoded)) return;
			printf("%s/", listRootDecoded.c_str());
#else
			printf("%s/", rListRoot.c_str());
#endif
		}
		
#ifdef WIN32
		{
			std::string fileName;
			if(!ConvertUtf8ToConsole(
				clear.GetClearFilename().c_str(), fileName))
				return;
			printf("%s", fileName.c_str());
		}
#else
		printf("%s", clear.GetClearFilename().c_str());
#endif
		
		if(!en->GetName().IsEncrypted())
		{
			printf("[FILENAME NOT ENCRYPTED]");
		}

		printf("\n");
		
		// Directory?
		if((en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) != 0)
		{
			// Recurse?
			if(opts[LIST_OPTION_RECURSIVE])
			{
				std::string subroot(rListRoot);
				if(!FirstLevel) subroot += '/';
				subroot += clear.GetClearFilename();
				List(en->GetObjectID(), subroot, opts, false /* not the first level to list */);
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::FindDirectoryObjectID(const std::string &)
//		Purpose: Find the object ID of a directory on the store, or return 0 for not found.
//				 If pStack != 0, the object is set to the stack of directories.
//				 Will start from the current directory stack.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
int64_t BackupQueries::FindDirectoryObjectID(const std::string &rDirName, bool AllowOldVersion,
			bool AllowDeletedDirs, std::vector<std::pair<std::string, int64_t> > *pStack)
{
	// Split up string into elements
	std::vector<std::string> dirElements;
	SplitString(rDirName, '/', dirElements);

	// Start from current stack, or root, whichever is required
	std::vector<std::pair<std::string, int64_t> > stack;
	int64_t dirID = BackupProtocolClientListDirectory::RootDirectory;
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
	int16_t excludeFlags = BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING;
	if(!AllowOldVersion) excludeFlags |= BackupProtocolClientListDirectory::Flags_OldVersion;
	if(!AllowDeletedDirs) excludeFlags |= BackupProtocolClientListDirectory::Flags_Deleted;

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
					dirID = (stack.size() > 0)?(stack[stack.size() - 1].second):BackupProtocolClientListDirectory::RootDirectory;
				}
				else
				{	
					// At root anyway
					dirID = BackupProtocolClientListDirectory::RootDirectory;
				}
			}
			else
			{
				// Not blank element. Read current directory.
				std::auto_ptr<BackupProtocolClientSuccess> dirreply(mrConnection.QueryListDirectory(
						dirID,
						BackupProtocolClientListDirectory::Flags_Dir,	// just directories
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
		return BackupProtocolClientListDirectory::RootDirectory;
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
		printf("Incorrect usage.\ncd [-o] [-d] <directory>\n");
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
		printf("Directory '%s' not found\n", args[0].c_str());
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
		printf("Incorrect usage.\nlcd <local-directory>\n");
		return;
	}
	
	// Try changing directory
#ifdef WIN32
	std::string dirName;
	if(!ConvertConsoleToUtf8(args[0].c_str(), dirName)) return;
	int result = ::chdir(dirName.c_str());
#else
	int result = ::chdir(args[0].c_str());
#endif
	if(result != 0)
	{
		printf((errno == ENOENT || errno == ENOTDIR)?"Directory '%s' does not exist\n":"Error changing dir to '%s'\n",
			args[0].c_str());
		return;
	}
	
	// Report current dir
	char wd[PATH_MAX];
	if(::getcwd(wd, PATH_MAX) == 0)
	{
		printf("Error getting current directory\n");
		return;
	}

#ifdef WIN32
	if(!ConvertUtf8ToConsole(wd, dirName)) return;
	printf("Local current directory is now '%s'\n", dirName.c_str());
#else
	printf("Local current directory is now '%s'\n", wd);
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
		printf("Incorrect usage.\ngetobject <object-id> <local-filename>\n");
		return;
	}
	
	int64_t id = ::strtoll(args[0].c_str(), 0, 16);
	if(id == LLONG_MIN || id == LLONG_MAX || id == 0)
	{
		printf("Not a valid object ID (specified in hex)\n");
		return;
	}
	
	// Does file exist?
	struct stat st;
	if(::stat(args[1].c_str(), &st) == 0 || errno != ENOENT)
	{
		printf("The local file %s already exists\n", args[1].c_str());
		return;
	}
	
	// Open file
	FileStream out(args[1].c_str(), O_WRONLY | O_CREAT | O_EXCL);
	
	// Request that object
	try
	{
		// Request object
		std::auto_ptr<BackupProtocolClientSuccess> getobj(mrConnection.QueryGetObject(id));
		if(getobj->GetObjectID() != BackupProtocolClientGetObject::NoObject)
		{
			// Stream that object out to the file
			std::auto_ptr<IOStream> objectStream(mrConnection.ReceiveStream());
			objectStream->CopyStreamTo(out);
			
			printf("Object ID %08llx fetched successfully.\n", id);
		}
		else
		{
			printf("Object does not exist on store.\n");
			::unlink(args[1].c_str());
		}
	}
	catch(...)
	{
		::unlink(args[1].c_str());
		printf("Error occured fetching object.\n");
	}
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandGet(const std::vector<std::string> &, const bool *)
//		Purpose: Command to get a file from the store
//		Created: 2003/10/12
//
// --------------------------------------------------------------------------
void BackupQueries::CommandGet(const std::vector<std::string> &args, const bool *opts)
{
	// At least one argument?
	// Check args
	if(args.size() < 1 || (opts['i'] && args.size() != 2) || args.size() > 2)
	{
		printf("Incorrect usage.\n"
			"get <remote-filename> [<local-filename>] or\n"
			"get -i <object-id> <local-filename>\n");
		return;
	}

	// Find object ID somehow
	int64_t fileId;
	int64_t dirId = GetCurrentDirectoryID();
	std::string localName;

	// BLOCK
	{
#ifdef WIN32
		std::string fileName;
		if(!ConvertConsoleToUtf8(args[0].c_str(), fileName))
			return;
#else
		std::string fileName(args[0]);
#endif

		if(!opts['i'])
		{
			// does this remote filename include a path?
			int index = fileName.rfind('/');
			if(index != std::string::npos)
			{
				std::string dirName(fileName.substr(0, index));
				fileName = fileName.substr(index + 1);

				dirId = FindDirectoryObjectID(dirName);
				if(dirId == 0)
				{
					printf("Directory '%s' not found\n", 
						dirName.c_str());
					return;
				}
			}
		}

		BackupStoreFilenameClear fn(fileName);

		// Need to look it up in the current directory
		mrConnection.QueryListDirectory(
				dirId,
				BackupProtocolClientListDirectory::Flags_File,	// just files
				(opts['i'])?(BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING):(BackupProtocolClientListDirectory::Flags_OldVersion | BackupProtocolClientListDirectory::Flags_Deleted), // only current versions
				false /* don't want attributes */);

		// Retrieve the directory from the stream following
		BackupStoreDirectory dir;
		std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
		dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

		if(opts['i'])
		{
			// Specified as ID. 
			fileId = ::strtoll(args[0].c_str(), 0, 16);
			if(fileId == LLONG_MIN || fileId == LLONG_MAX || 
				fileId == 0)
			{
				printf("Not a valid object ID (specified in hex)\n");
				return;
			}
			
			// Check that the item is actually in the directory
			if(dir.FindEntryByID(fileId) == 0)
			{
				printf("ID '%08llx' not found in current "
					"directory on store.\n"
					"(You can only download objects by ID "
					"from the current directory.)\n", 
					fileId);
				return;
			}
			
			// Must have a local name in the arguments (check at beginning of function ensures this)
			localName = args[1];
		}
		else
		{				
			// Specified by name, find the object in the directory to get the ID
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.FindMatchingClearName(fn);
			
			if(en == 0)
			{
				printf("Filename '%s' not found in current "
					"directory on store.\n"
					"(Subdirectories in path not "
					"searched.)\n", args[0].c_str());
				return;
			}
			
			fileId = en->GetObjectID();
			
			// Local name is the last argument, which is either 
			// the looked up filename, or a filename specified 
			// by the user.
			localName = args[args.size() - 1];
		}
	}
	
	// Does local file already exist? (don't want to overwrite)
	struct stat st;
	if(::stat(localName.c_str(), &st) == 0 || errno != ENOENT)
	{
		printf("The local file %s already exists, will not overwrite it.\n", localName.c_str());
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
		printf("Object ID %08llx fetched sucessfully.\n", fileId);
	}
	catch(...)
	{
		::unlink(localName.c_str());
		printf("Error occured fetching file.\n");
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
BackupQueries::CompareParams::CompareParams()
	: mQuickCompare(false),
	  mIgnoreExcludes(false),
	  mIgnoreAttributes(false),
	  mDifferences(0),
	  mDifferencesExplainedByModTime(0),
	  mExcludedDirs(0),
	  mExcludedFiles(0),
	  mpExcludeFiles(0),
	  mpExcludeDirs(0),
	  mLatestFileUploadTime(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CompareParams::~CompareParams()
//		Purpose: Destructor
//		Created: 29/1/04
//
// --------------------------------------------------------------------------
BackupQueries::CompareParams::~CompareParams()
{
	DeleteExcludeLists();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CompareParams::DeleteExcludeLists()
//		Purpose: Delete the include lists contained
//		Created: 29/1/04
//
// --------------------------------------------------------------------------
void BackupQueries::CompareParams::DeleteExcludeLists()
{
	if(mpExcludeFiles != 0)
	{
		delete mpExcludeFiles;
		mpExcludeFiles = 0;
	}
	if(mpExcludeDirs != 0)
	{
		delete mpExcludeDirs;
		mpExcludeDirs = 0;
	}
}


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
	// Parameters, including count of differences
	BackupQueries::CompareParams params;
	params.mQuickCompare = opts['q'];
	params.mIgnoreExcludes = opts['E'];
	params.mIgnoreAttributes = opts['A'];
	
	// Try and work out the time before which all files should be on the server
	{
		std::string syncTimeFilename(mrConfiguration.GetKeyValue("DataDirectory") + DIRECTORY_SEPARATOR_ASCHAR);
		syncTimeFilename += "last_sync_start";
		// Stat it to get file time
		struct stat st;
		if(::stat(syncTimeFilename.c_str(), &st) == 0)
		{
			// Files modified after this time shouldn't be on the server, so report errors slightly differently
			params.mLatestFileUploadTime = FileModificationTime(st)
					- SecondsToBoxTime(mrConfiguration.GetKeyValueInt("MinimumFileAge"));
		}
		else
		{
			printf("Warning: couldn't determine the time of the last synchronisation -- checks not performed.\n");
		}
	}

	// Quick compare?
	if(params.mQuickCompare)
	{
		printf("WARNING: Quick compare used -- file attributes are not checked.\n");
	}
	
	if(!opts['l'] && opts['a'] && args.size() == 0)
	{
		// Compare all locations
		const Configuration &locations(mrConfiguration.GetSubConfiguration("BackupLocations"));
		for(std::list<std::pair<std::string, Configuration> >::const_iterator i = locations.mSubConfigurations.begin();
				i != locations.mSubConfigurations.end(); ++i)
		{
			CompareLocation(i->first, params);
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
		if(!params.mIgnoreExcludes)
		{
			printf("Cannot use excludes on directory to directory comparison -- use -E flag to specify ignored excludes\n");
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
		printf("Incorrect usage.\ncompare -a\n or compare -l <location-name>\n or compare <store-dir-name> <local-dir-name>\n");
		return;
	}
	
	printf("\n[ %d (of %d) differences probably due to file modifications after the last upload ]\nDifferences: %d (%d dirs excluded, %d files excluded)\n",
		params.mDifferencesExplainedByModTime, params.mDifferences, params.mDifferences, params.mExcludedDirs, params.mExcludedFiles);
	
	// Set return code?
	if(opts['c'])
	{
		SetReturnCode((params.mDifferences == 0)?COMPARE_RETURN_SAME:COMPARE_RETURN_DIFFERENT);
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
void BackupQueries::CompareLocation(const std::string &rLocation, BackupQueries::CompareParams &rParams)
{
	// Find the location's sub configuration
	const Configuration &locations(mrConfiguration.GetSubConfiguration("BackupLocations"));
	if(!locations.SubConfigurationExists(rLocation.c_str()))
	{
		printf("Location %s does not exist.\n", rLocation.c_str());
		return;
	}
	const Configuration &loc(locations.GetSubConfiguration(rLocation.c_str()));
	
	try
	{
		// Generate the exclude lists
		if(!rParams.mIgnoreExcludes)
		{
			rParams.mpExcludeFiles = BackupClientMakeExcludeList_Files(loc);
			rParams.mpExcludeDirs = BackupClientMakeExcludeList_Dirs(loc);
		}
				
		// Then get it compared
		Compare(std::string("/") + rLocation, 
			loc.GetKeyValue("Path"), rParams);
	}
	catch(...)
	{
		// Clean up
		rParams.DeleteExcludeLists();
		throw;
	}
	
	// Delete exclude lists
	rParams.DeleteExcludeLists();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::Compare(const std::string &, const std::string &, BackupQueries::CompareParams &)
//		Purpose: Compare a store directory against a local directory
//		Created: 2003/10/13
//
// --------------------------------------------------------------------------
void BackupQueries::Compare(const std::string &rStoreDir, const std::string &rLocalDir, BackupQueries::CompareParams &rParams)
{
#ifdef WIN32
	std::string storeDirEncoded;
	if(!ConvertConsoleToUtf8(rStoreDir.c_str(), storeDirEncoded)) return;
#else
	const std::string& storeDirEncoded(rStoreDir);
#endif
	
	// Get the directory ID of the directory -- only use current data
	int64_t dirID = FindDirectoryObjectID(storeDirEncoded);
	
	// Found?
	if(dirID == 0)
	{
		printf("Local directory '%s' exists, but "
			"server directory '%s' does not exist\n", 
			rLocalDir.c_str(), rStoreDir.c_str());		
		rParams.mDifferences ++;
		return;
	}

#ifdef WIN32
	std::string localDirEncoded;
	if(!ConvertConsoleToUtf8(rLocalDir.c_str(), localDirEncoded)) return;
#else
	std::string localDirEncoded(rLocalDir);
#endif
	
	// Go!
	Compare(dirID, storeDirEncoded, localDirEncoded, rParams);
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
void BackupQueries::Compare(int64_t DirID, const std::string &rStoreDir, const std::string &rLocalDir, BackupQueries::CompareParams &rParams)
{
#ifdef WIN32
	// By this point, rStoreDir and rLocalDir should be in UTF-8 encoding

	std::string localDirDisplay;
	std::string storeDirDisplay;

	if(!ConvertUtf8ToConsole(rLocalDir.c_str(), localDirDisplay)) return;
	if(!ConvertUtf8ToConsole(rStoreDir.c_str(), storeDirDisplay)) return;
#else
	const std::string& localDirDisplay(rLocalDir);
	const std::string& storeDirDisplay(rStoreDir);
#endif

	// Get info on the local directory
	struct stat st;
	if(::lstat(rLocalDir.c_str(), &st) != 0)
	{
		// What kind of error?
		if(errno == ENOTDIR)
		{
			printf("Local object '%s' is a file, "
				"server object '%s' is a directory\n", 
				localDirDisplay.c_str(), 
				storeDirDisplay.c_str());
			rParams.mDifferences ++;
		}
		else if(errno == ENOENT)
		{
			printf("Local directory '%s' does not exist "
				"(compared to server directory '%s')\n",
				localDirDisplay.c_str(), 
				storeDirDisplay.c_str());
		}
		else
		{
			printf("ERROR: stat on local dir '%s'\n", 
				localDirDisplay.c_str());
		}
		return;
	}

	// Get the directory listing from the store
	mrConnection.QueryListDirectory(
			DirID,
			BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,	// get everything
			BackupProtocolClientListDirectory::Flags_OldVersion | BackupProtocolClientListDirectory::Flags_Deleted,	// except for old versions and deleted files
			true /* want attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(mrConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, mrConnection.GetTimeout());

	// Test out the attributes
	if(!dir.HasAttributes())
	{
		printf("Store directory '%s' doesn't have attributes.\n", 
			storeDirDisplay.c_str());
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

		if(!(attr.Compare(localAttr, true, true /* ignore modification times */)))
		{
			printf("Local directory '%s' has different attributes "
				"to store directory '%s'.\n",
				localDirDisplay.c_str(), 
				storeDirDisplay.c_str());
			rParams.mDifferences ++;
		}
	}

	// Open the local directory
	DIR *dirhandle = ::opendir(rLocalDir.c_str());
	if(dirhandle == 0)
	{
		printf("ERROR: opendir on local dir '%s'\n", 
			localDirDisplay.c_str());
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
				continue;
			}

#ifndef HAVE_VALID_DIRENT_D_TYPE
			std::string fn(rLocalDir);
			fn += DIRECTORY_SEPARATOR_ASCHAR;
			fn += localDirEn->d_name;
			struct stat st;
			if(::lstat(fn.c_str(), &st) != 0)
			{
			    THROW_EXCEPTION(CommonException, OSFileError)
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
			printf("ERROR: closedir on local dir '%s'\n", 
				localDirDisplay.c_str());
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
#ifdef WIN32
			// File name is also in UTF-8 encoding, 
			// need to convert to console
			std::string fileNameDisplay;
			if(!ConvertUtf8ToConsole(i->first.c_str(), 
				fileNameDisplay)) return;
#else
			const std::string& fileNameDisplay(i->first);
#endif

			std::string localPathDisplay = localDirDisplay +
				DIRECTORY_SEPARATOR + fileNameDisplay;
			std::string storePathDisplay = storeDirDisplay +
				"/" + fileNameDisplay;

			// Does the file exist locally?
			string_set_iter_t local(localFiles.find(fileName));
			if(local == localFiles.end())
			{
				// Not found -- report
				printf("Local file '%s' does not exist, "
					"but store file '%s' does.\n",
					localPathDisplay.c_str(),
					storePathDisplay.c_str());
				rParams.mDifferences ++;
			}
			else
			{
				try
				{
					// make local name of file for comparison
					std::string localPath(rLocalDir + DIRECTORY_SEPARATOR + fileName);

					// Files the same flag?
					bool equal = true;
					
					// File modified after last sync flag
					bool modifiedAfterLastSync = false;
						
					if(rParams.mQuickCompare)
					{
						// Compare file -- fetch it
						mrConnection.QueryGetBlockIndexByID(i->second->GetObjectID());

						// Stream containing block index
						std::auto_ptr<IOStream> blockIndexStream(mrConnection.ReceiveStream());
						
						// Compare
						equal = BackupStoreFile::CompareFileContentsAgainstBlockIndex(localPath.c_str(), *blockIndexStream, mrConnection.GetTimeout());
					}
					else
					{
						// Compare file -- fetch it
						mrConnection.QueryGetFile(DirID, i->second->GetObjectID());
	
						// Stream containing encoded file
						std::auto_ptr<IOStream> objectStream(mrConnection.ReceiveStream());
	
						// Decode it
						std::auto_ptr<BackupStoreFile::DecodedStream> fileOnServerStream;
						// Got additional attibutes?
						if(i->second->HasAttributes())
						{
							// Use these attributes
							const StreamableMemBlock &storeAttr(i->second->GetAttributes());
							BackupClientFileAttributes attr(storeAttr);
							fileOnServerStream.reset(BackupStoreFile::DecodeFileStream(*objectStream, mrConnection.GetTimeout(), &attr).release());
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
						box_time_t fileModTime = 0;
						BackupClientFileAttributes localAttr;
						localAttr.ReadAttributes(localPath.c_str(), false /* don't zero mod times */, &fileModTime);					
						modifiedAfterLastSync = (fileModTime > rParams.mLatestFileUploadTime);
						if(!rParams.mIgnoreAttributes &&
						   !localAttr.Compare(fileOnServerStream->GetAttributes(),
								true /* ignore attr mod time */,
								fileOnServerStream->IsSymLink() /* ignore modification time if it's a symlink */))
						{
							printf("Local file '%s' "
								"has different attributes "
								"to store file '%s'.\n",
								localPathDisplay.c_str(), 
								storePathDisplay.c_str());						
							rParams.mDifferences ++;
							if(modifiedAfterLastSync)
							{
								rParams.mDifferencesExplainedByModTime ++;
								printf("(the file above was modified after the last sync time -- might be reason for difference)\n");
							}
							else if(i->second->HasAttributes())
							{
								printf("(the file above has had new attributes applied)\n");
							}
						}
	
						// Compare contents, if it's a regular file not a link
						// Remember, we MUST read the entire stream from the server.
						if(!fileOnServerStream->IsSymLink())
						{
							// Open the local file
							FileStream l(localPath.c_str());
							
							// Size
							IOStream::pos_type fileSizeLocal = l.BytesLeftToRead();
							IOStream::pos_type fileSizeServer = 0;
							
							// Test the contents
							char buf1[2048];
							char buf2[2048];
							while(fileOnServerStream->StreamDataLeft() && l.StreamDataLeft())
							{
								int size = fileOnServerStream->Read(buf1, sizeof(buf1), mrConnection.GetTimeout());
								fileSizeServer += size;
								
								if(l.Read(buf2, size) != size
										|| ::memcmp(buf1, buf2, size) != 0)
								{
									equal = false;
									break;
								}
							}
	
							// Check read all the data from the server and file -- can't be equal if local and remote aren't the same length
							// Can't use StreamDataLeft() test on file, because if it's the same size, it won't know
							// it's EOF yet.
							if(fileOnServerStream->StreamDataLeft() || fileSizeServer != fileSizeLocal)
							{
								equal = false;
							}

							// Must always read the entire decoded string, if it's not a symlink
							if(fileOnServerStream->StreamDataLeft())
							{
								// Absorb all the data remaining
								char buffer[2048];
								while(fileOnServerStream->StreamDataLeft())
								{
									fileOnServerStream->Read(buffer, sizeof(buffer), mrConnection.GetTimeout());
								}
							}
						}
					}

					// Report if not equal.
					if(!equal)
					{
						printf("Local file '%s' "
							"has different contents "
							"to store file '%s'.\n",
							localPathDisplay.c_str(), 
							storePathDisplay.c_str());
						rParams.mDifferences ++;
						if(modifiedAfterLastSync)
						{
							rParams.mDifferencesExplainedByModTime ++;
							printf("(the file above was modified after the last sync time -- might be reason for difference)\n");
						}
						else if(i->second->HasAttributes())
						{
							printf("(the file above has had new attributes applied)\n");
						}
					}
				}
				catch(BoxException &e)
				{
					printf("ERROR: (%d/%d) during file fetch and comparison for '%s'\n",
						e.GetType(),
						e.GetSubType(),
						storePathDisplay.c_str());
				}
				catch(...)
				{
					printf("ERROR: (unknown) during file fetch and comparison for '%s'\n", storePathDisplay.c_str());
				}

				// Remove from set so that we know it's been compared
				localFiles.erase(local);
			}
		}
		
		// Report any files which exist on the locally, but not on the store
		for(string_set_iter_t i = localFiles.begin(); i != localFiles.end(); ++i)
		{
#ifdef WIN32
			// File name is also in UTF-8 encoding, 
			// need to convert to console
			std::string fileNameDisplay;
			if(!ConvertUtf8ToConsole(i->c_str(), fileNameDisplay)) 
				return;
#else
			const std::string& fileNameDisplay(*i);
#endif

			std::string localPath(rLocalDir + 
				DIRECTORY_SEPARATOR + *i);
			std::string localPathDisplay(localDirDisplay +
				DIRECTORY_SEPARATOR + fileNameDisplay);
			std::string storePathDisplay(storeDirDisplay +
				"/" + fileNameDisplay);

			// Should this be ignored (ie is excluded)?
			if(rParams.mpExcludeFiles == 0 || 
				!(rParams.mpExcludeFiles->IsExcluded(localPath)))
			{
				printf("Local file '%s' exists, "
					"but store file '%s' "
					"does not exist.\n",
					localPathDisplay.c_str(),
					storePathDisplay.c_str());
				rParams.mDifferences ++;
				
				// Check the file modification time
				{
					struct stat st;
					if(::stat(localPath.c_str(), &st) == 0)
					{
						if(FileModificationTime(st) > rParams.mLatestFileUploadTime)
						{
							rParams.mDifferencesExplainedByModTime ++;
							printf("(the file above was modified after the last sync time -- might be reason for difference)\n");
						}
					}
				}
			}
			else
			{
				rParams.mExcludedFiles ++;
			}
		}		
		
		// Finished with the files, clear the sets to reduce memory usage slightly
		localFiles.clear();
		storeFiles.clear();
		
		// Now do the directories, recusively to check subdirectories
		for(std::set<std::pair<std::string, BackupStoreDirectory::Entry *> >::const_iterator i = storeDirs.begin(); i != storeDirs.end(); ++i)
		{
#ifdef WIN32
			// Directory name is also in UTF-8 encoding, 
			// need to convert to console
			std::string subdirNameDisplay;
			if(!ConvertUtf8ToConsole(i->first.c_str(), 
				subdirNameDisplay))
				return;
#else
			const std::string& subdirNameDisplay(i->first);
#endif

			std::string localPathDisplay = localDirDisplay +
				DIRECTORY_SEPARATOR + subdirNameDisplay;
			std::string storePathDisplay = storeDirDisplay +
				"/" + subdirNameDisplay;

			// Does the directory exist locally?
			string_set_iter_t local(localDirs.find(i->first));
			if(local == localDirs.end())
			{
				// Not found -- report
				printf("Local directory '%s' does not exist, "
					"but store directory '%s' does.\n",
					localPathDisplay.c_str(),
					storePathDisplay.c_str());
				rParams.mDifferences ++;
			}
			else
			{
				// Compare directory
				Compare(i->second->GetObjectID(), rStoreDir + "/" + i->first, rLocalDir + DIRECTORY_SEPARATOR + i->first, rParams);
				
				// Remove from set so that we know it's been compared
				localDirs.erase(local);
			}
		}
		
		// Report any files which exist on the locally, but not on the store
		for(std::set<std::string>::const_iterator i = localDirs.begin(); i != localDirs.end(); ++i)
		{
#ifdef WIN32
			// File name is also in UTF-8 encoding, 
			// need to convert to console
			std::string fileNameDisplay;
			if(!ConvertUtf8ToConsole(i->c_str(), fileNameDisplay))
				return;
#else
			const std::string& fileNameDisplay(*i);
#endif

			std::string localPath = rLocalDir +
				DIRECTORY_SEPARATOR + *i;
			std::string storePath = rStoreDir +
				"/" + *i;

			std::string localPathDisplay = localDirDisplay +
				DIRECTORY_SEPARATOR + fileNameDisplay;
			std::string storePathDisplay = storeDirDisplay +
				"/" + fileNameDisplay;

			// Should this be ignored (ie is excluded)?
			if(rParams.mpExcludeDirs == 0 || !(rParams.mpExcludeDirs->IsExcluded(localPath)))
			{
				printf("Local directory '%s' exists, but "
					"store directory '%s' does not exist.\n",
					localPathDisplay.c_str(),
					storePathDisplay.c_str());
				rParams.mDifferences ++;
			}
			else
			{
				rParams.mExcludedDirs ++;
			}
		}		
		
	}
	catch(...)
	{
		if(dirhandle != 0)
		{
			::closedir(dirhandle);
		}
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
	if(args.size() != 2)
	{
		printf("Incorrect usage.\nrestore [-d] [-r] [-i] <directory-name> <local-directory-name>\n");
		return;
	}

	// Restoring deleted things?
	bool restoreDeleted = opts['d'];

	// Get directory ID
	int64_t dirID = 0;
	if(opts['i'])
	{
		// Specified as ID. 
		dirID = ::strtoll(args[0].c_str(), 0, 16);
		if(dirID == LLONG_MIN || dirID == LLONG_MAX || dirID == 0)
		{
			printf("Not a valid object ID (specified in hex)\n");
			return;
		}
	}
	else
	{
#ifdef WIN32
		std::string storeDirEncoded;
		if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded))
			return;
#else
		const std::string& storeDirEncoded(args[0]);
#endif
	
		// Look up directory ID
		dirID = FindDirectoryObjectID(storeDirEncoded, 
			false /* no old versions */, 
			restoreDeleted /* find deleted dirs */);
	}
	
	// Allowable?
	if(dirID == 0)
	{
		printf("Directory '%s' not found on server\n", args[0].c_str());
		return;
	}
	if(dirID == BackupProtocolClientListDirectory::RootDirectory)
	{
		printf("Cannot restore the root directory -- restore locations individually.\n");
		return;
	}
	
#ifdef WIN32
	std::string localName;
	if(!ConvertConsoleToUtf8(args[1].c_str(), localName)) return;
#else
	std::string localName(args[1]);
#endif

	// Go and restore...
	switch(BackupClientRestore(mrConnection, dirID, localName.c_str(), 
		true /* print progress dots */, restoreDeleted, 
		false /* don't undelete after restore! */, 
		opts['r'] /* resume? */))
	{
	case Restore_Complete:
		printf("Restore complete\n");
		break;
	
	case Restore_ResumePossible:
		printf("Resume possible -- repeat command with -r flag to resume\n");
		break;
	
	case Restore_TargetExists:
		printf("The target directory exists. You cannot restore over an existing directory.\n");
		break;
		
	case Restore_TargetPathNotFound:
		printf("The target directory path does not exist.\n"
			"To restore to a directory whose parent "
			"does not exist, create the parent first.\n");
		break;

	default:
		printf("ERROR: Unknown restore result.\n");
		break;
	}
}



// These are autogenerated by a script.
extern char *help_commands[];
extern char *help_text[];


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
void BackupQueries::CommandUsage()
{
	// Request full details from the server
	std::auto_ptr<BackupProtocolClientAccountUsage> usage(mrConnection.QueryGetAccountUsage());

	// Display each entry in turn
	int64_t hardLimit = usage->GetBlocksHardLimit();
	int32_t blockSize = usage->GetBlockSize();
	CommandUsageDisplayEntry("Used", usage->GetBlocksUsed(), hardLimit, blockSize);
	CommandUsageDisplayEntry("Old files", usage->GetBlocksInOldFiles(), hardLimit, blockSize);
	CommandUsageDisplayEntry("Deleted files", usage->GetBlocksInDeletedFiles(), hardLimit, blockSize);
	CommandUsageDisplayEntry("Directories", usage->GetBlocksInDirectories(), hardLimit, blockSize);
	CommandUsageDisplayEntry("Soft limit", usage->GetBlocksSoftLimit(), hardLimit, blockSize);
	CommandUsageDisplayEntry("Hard limit", hardLimit, hardLimit, blockSize);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupQueries::CommandUsageDisplayEntry(const char *, int64_t, int64_t, int32_t)
//		Purpose: Display an entry in the usage table
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
void BackupQueries::CommandUsageDisplayEntry(const char *Name, int64_t Size, int64_t HardLimit, int32_t BlockSize)
{
	// Calculate size in Mb
	double mb = (((double)Size) * ((double)BlockSize)) / ((double)(1024*1024));
	int64_t percent = (Size * 100) / HardLimit;

	// Bar graph
	char bar[41];
	unsigned int b = (int)((Size * (sizeof(bar)-1)) / HardLimit);
	if(b > sizeof(bar)-1) {b = sizeof(bar)-1;}
	for(unsigned int l = 0; l < b; l++)
	{
		bar[l] = '*';
	}
	bar[b] = '\0';

	// Print the entryj
	::printf("%14s %10.1fMb %3d%% %s\n", Name, mb, (int32_t)percent, bar);
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
	// Check arguments
	if(args.size() != 1)
	{
		printf("Incorrect usage.\nundelete <directory-name>\n");
		return;
	}

#ifdef WIN32
	std::string storeDirEncoded;
	if(!ConvertConsoleToUtf8(args[0].c_str(), storeDirEncoded)) return;
#else
	const std::string& storeDirEncoded(args[0]);
#endif
	
	// Get directory ID
	int64_t dirID = FindDirectoryObjectID(storeDirEncoded, 
		false /* no old versions */, true /* find deleted dirs */);
	
	// Allowable?
	if(dirID == 0)
	{
		printf("Directory '%s' not found on server\n", args[0].c_str());
		return;
	}
	if(dirID == BackupProtocolClientListDirectory::RootDirectory)
	{
		printf("Cannot undelete the root directory.\n");
		return;
	}

	// Undelete
	mrConnection.QueryUndeleteDirectory(dirID);
}
