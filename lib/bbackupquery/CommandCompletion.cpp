// --------------------------------------------------------------------------
//
// File
//		Name:    CommandCompletion.cpp
//		Purpose: Parts of BackupQueries that depend on readline
//		Created: 2011/01/21
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_LIBREADLINE
	#ifdef HAVE_READLINE_READLINE_H
		#include <readline/readline.h>
	#elif defined(HAVE_EDITLINE_READLINE_H)
		#include <editline/readline.h>
	#elif defined(HAVE_READLINE_H)
		#include <readline.h>
	#endif
#endif

#ifdef HAVE_READLINE_HISTORY
	#ifdef HAVE_READLINE_HISTORY_H
		#include <readline/history.h>
	#elif defined(HAVE_HISTORY_H)
		#include <history.h>
	#endif
#endif

#include <cstring>
#include <string>

#include "BackupQueries.h"
#include "Configuration.h"

#include "autogen_BackupProtocol.h"

#include "MemLeakFindOn.h"

#define COMPARE_RETURN_SAME		1
#define COMPARE_RETURN_DIFFERENT	2
#define COMPARE_RETURN_ERROR		3
#define COMMAND_RETURN_ERROR		4

#define COMPLETION_FUNCTION(name, code) \
std::vector<std::string> Complete ## name( \
	BackupQueries::ParsedCommand& rCommand, \
	const std::string& prefix, \
	BackupProtocolCallable& rProtocol, const Configuration& rConfig, \
	BackupQueries& rQueries) \
{ \
	std::vector<std::string> completions; \
	\
	try \
	{ \
		code \
	} \
	catch(std::exception &e) \
	{ \
		BOX_TRACE("Failed to complete " << prefix << ": " << e.what()); \
	} \
	catch(...) \
	{ \
		BOX_TRACE("Failed to complete " << prefix << ": " \
			"unknown error"); \
	} \
	\
	return completions; \
}

#define DELEGATE_COMPLETION(name) \
	completions = Complete ## name(rCommand, prefix, rProtocol, rConfig, \
	rQueries);

COMPLETION_FUNCTION(None,)

#ifdef HAVE_RL_FILENAME_COMPLETION_FUNCTION
	#define RL_FILENAME_COMPLETION_FUNCTION rl_filename_completion_function
	#define HAVE_A_FILENAME_COMPLETION_FUNCTION 1
#elif defined HAVE_FILENAME_COMPLETION_FUNCTION
	#define RL_FILENAME_COMPLETION_FUNCTION filename_completion_function
	#define HAVE_A_FILENAME_COMPLETION_FUNCTION 1
#endif

#ifdef HAVE_A_FILENAME_COMPLETION_FUNCTION
COMPLETION_FUNCTION(Default,
	int i = 0;
	
	while (const char *match = RL_FILENAME_COMPLETION_FUNCTION(prefix.c_str(), i))
	{
		completions.push_back(match);
		++i;
	}
)
#else // !HAVE_A_FILENAME_COMPLETION_FUNCTION
COMPLETION_FUNCTION(Default,)
#endif // HAVE_A_FILENAME_COMPLETION_FUNCTION

COMPLETION_FUNCTION(Command,
	int len = prefix.length();

	for(int i = 0; commands[i].name != NULL; i++)
	{
		if(::strncmp(commands[i].name, prefix.c_str(), len) == 0)
		{
			completions.push_back(commands[i].name);
		}
	}
)

void CompleteOptionsInternal(const std::string& prefix,
	BackupQueries::ParsedCommand& rCommand,
	std::vector<std::string>& completions)
{
	std::string availableOptions = rCommand.pSpec->opts;

	for(std::string::iterator
		opt =  availableOptions.begin();
		opt != availableOptions.end(); opt++)
	{
		if(rCommand.mOptions.find(*opt) == std::string::npos)
		{
			if(prefix == "")
			{
				// complete with possible option strings
				completions.push_back(std::string("-") + *opt);
			}
			else
			{
				// complete with possible additional options
				completions.push_back(prefix + *opt);
			}
		}
	}
}

COMPLETION_FUNCTION(Options,
	CompleteOptionsInternal(prefix, rCommand, completions);
)

std::string EncodeFileName(const std::string &rUnEncodedName)
{
#ifdef WIN32
	std::string encodedName;
	if(!ConvertConsoleToUtf8(rUnEncodedName, encodedName))
	{
		return std::string();
	}
	return encodedName;
#else
	return rUnEncodedName;
#endif
}

int16_t GetExcludeFlags(BackupQueries::ParsedCommand& rCommand)
{
	int16_t excludeFlags = 0;

	if (rCommand.mOptions.find(LIST_OPTION_ALLOWOLD) == std::string::npos)
	{
		excludeFlags |= BackupProtocolListDirectory::Flags_OldVersion;
	}

	if (rCommand.mOptions.find(LIST_OPTION_ALLOWDELETED) == std::string::npos)
	{
		excludeFlags |= BackupProtocolListDirectory::Flags_Deleted;
	}

	return excludeFlags;
}

std::vector<std::string> CompleteRemoteFileOrDirectory(
	BackupQueries::ParsedCommand& rCommand,
	const std::string& prefix, BackupProtocolCallable& rProtocol,
	BackupQueries& rQueries, int16_t includeFlags)
{
	std::vector<std::string> completions;
	
	// default to using the current directory
	int64_t listDirId = rQueries.GetCurrentDirectoryID();
	std::string searchPrefix;
	std::string listDir = prefix;

	if(rCommand.mCompleteArgCount == rCommand.mCmdElements.size())
	{
		// completing an empty name, from the current directory
		// nothing to change
	}
	else
	{
		// completing a partially-completed subdirectory name
		searchPrefix = prefix;
		listDir = "";

		// do we need to list a subdirectory to complete?
		size_t lastSlash = searchPrefix.rfind('/');
		if(lastSlash == std::string::npos)
		{
			// no slashes, so the whole name is the prefix
			// nothing to change
		}
		else
		{
			// listing a partially-completed subdirectory name
			listDir = searchPrefix.substr(0, lastSlash);

			listDirId = rQueries.FindDirectoryObjectID(listDir,
				rCommand.mOptions.find(LIST_OPTION_ALLOWOLD)
					!= std::string::npos,
				rCommand.mOptions.find(LIST_OPTION_ALLOWDELETED)
					!= std::string::npos);

			if(listDirId == 0)
			{
				// no matches for subdir to list,
				// return empty-handed.
				return completions;
			}

			// matched, and updated listDir and listDirId already
			searchPrefix = searchPrefix.substr(lastSlash + 1);
		}
	}

	// Always include directories, because they contain files.
	// We will append a slash later for each directory if we're
	// actually looking for files.
	//
	// If we're looking for directories, then only list directories.

	bool completeFiles = includeFlags &
		BackupProtocolListDirectory::Flags_File;
	bool completeDirs = includeFlags &
		BackupProtocolListDirectory::Flags_Dir;
	int16_t listFlags = 0;

	if(completeFiles)
	{
		listFlags = BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING;
	}
	else if(completeDirs)
	{
		listFlags = BackupProtocolListDirectory::Flags_Dir;
	}

	rProtocol.QueryListDirectory(listDirId,
		listFlags, GetExcludeFlags(rCommand),
		false /* no attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(rProtocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, rProtocol.GetTimeout());

	// Then... display everything
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next()) != 0)
	{
		BackupStoreFilenameClear clear(en->GetName());
		std::string name = clear.GetClearFilename().c_str();
		if(name.compare(0, searchPrefix.length(), searchPrefix) == 0)
		{
			bool dir_added = false;

			if(en->IsDir() &&
				(includeFlags & BackupProtocolListDirectory::Flags_Dir) == 0)
			{
				// Was looking for a file, but this is a 
				// directory, so append a slash to the name
				name += "/";
			}

			#ifdef HAVE_LIBREADLINE
			if(strchr(name.c_str(), ' '))
			{
				int n_quote = 0;

				for(int k = strlen(rl_line_buffer); k >= 0; k--)
				{
					if (rl_line_buffer[k] == '\"') {
						++n_quote;
					}
				}

				dir_added = false;

				if (!(n_quote % 2))
				{
					name = "\"" + (listDir == "" ? name : listDir + "/" + name);
					dir_added = true;
				}

				name = name + "\"";
			}
			#endif

			if(listDir == "" || dir_added)
			{
				completions.push_back(name);
			}
			else
			{
				completions.push_back(listDir + "/" + name);
			}
		}
	}

	return completions;
}

COMPLETION_FUNCTION(RemoteDir,
	completions = CompleteRemoteFileOrDirectory(rCommand, prefix,
		rProtocol, rQueries,
		BackupProtocolListDirectory::Flags_Dir);
)

COMPLETION_FUNCTION(RemoteFile,
	completions = CompleteRemoteFileOrDirectory(rCommand, prefix,
		rProtocol, rQueries,
		BackupProtocolListDirectory::Flags_File);
)

COMPLETION_FUNCTION(LocalDir,
	DELEGATE_COMPLETION(Default);
)

COMPLETION_FUNCTION(LocalFile,
	DELEGATE_COMPLETION(Default);
)

COMPLETION_FUNCTION(LocationName,
	const Configuration &locations(rConfig.GetSubConfiguration(
		"BackupLocations"));

	std::vector<std::string> locNames =
		locations.GetSubConfigurationNames();

	for(std::vector<std::string>::iterator
		pLocName  = locNames.begin();
		pLocName != locNames.end();
		pLocName++)
	{
		if(pLocName->compare(0, pLocName->length(), prefix) == 0)
		{
			completions.push_back(*pLocName);
		}
	}
)

COMPLETION_FUNCTION(RemoteFileIdInCurrentDir,
	int64_t listDirId = rQueries.GetCurrentDirectoryID();
	int16_t excludeFlags = GetExcludeFlags(rCommand);

	rProtocol.QueryListDirectory(
		listDirId,
		BackupProtocolListDirectory::Flags_File,
		excludeFlags, false /* no attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(rProtocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, rProtocol.GetTimeout());

	// Then... compare each item
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next()) != 0)
	{
		std::ostringstream hexId;
		hexId << std::hex << en->GetObjectID();
		if(hexId.str().compare(0, prefix.length(), prefix) == 0)
		{
			completions.push_back(hexId.str());
		}
	}
)

// TODO implement completion of hex IDs up to the maximum according to Usage
COMPLETION_FUNCTION(RemoteId,)

COMPLETION_FUNCTION(GetFileOrId,
	if(rCommand.mOptions.find('i') != std::string::npos)
	{
		DELEGATE_COMPLETION(RemoteFileIdInCurrentDir);
	}
	else
	{
		DELEGATE_COMPLETION(RemoteFile);
	}
)

COMPLETION_FUNCTION(CompareLocationOrRemoteDir,
	if(rCommand.mOptions.find('l') != std::string::npos)
	{
		DELEGATE_COMPLETION(LocationName);
	}
	else
	{
		DELEGATE_COMPLETION(RemoteDir);
	}
)

COMPLETION_FUNCTION(CompareNoneOrLocalDir,
	if(rCommand.mOptions.find('l') != std::string::npos)
	{
		// no completions
		DELEGATE_COMPLETION(None);
	}
	else
	{
		DELEGATE_COMPLETION(LocalDir);
	}
)

COMPLETION_FUNCTION(RestoreRemoteDirOrId,
	if(rCommand.mOptions.find('i') != std::string::npos)
	{
		DELEGATE_COMPLETION(RemoteId);
	}
	else
	{
		DELEGATE_COMPLETION(RemoteDir);
	}
)

// Data about commands
QueryCommandSpecification commands[] = 
{
	{ "quit",	"",		Command_Quit, 	{} },
	{ "exit",	"",		Command_Quit,	{} },
	{ "list",	"adDFhiIorRsStTU",	Command_List,	{CompleteRemoteDir} },
	{ "pwd",	"",		Command_pwd,	{} },
	{ "cd",		"od",		Command_cd,	{CompleteRemoteDir} },
	{ "lcd",	"",		Command_lcd,	{CompleteLocalDir} },
	{ "sh", 	"",		Command_sh,	{CompleteDefault} },
	{ "getobject",	"",		Command_GetObject,
		{CompleteRemoteId, CompleteLocalDir} },
	{ "get",	"i",		Command_Get,
		{CompleteGetFileOrId, CompleteLocalDir} },
	{ "compare",	"alcqAEQ",	Command_Compare,
		{CompleteCompareLocationOrRemoteDir, CompleteCompareNoneOrLocalDir} },
	{ "restore",	"drif",		Command_Restore,
		{CompleteRestoreRemoteDirOrId, CompleteLocalDir} },
	{ "help",	"",		Command_Help,	{} },
	{ "usage",	"m",		Command_Usage,	{} },
	{ "undelete",	"i",		Command_Undelete,
		{CompleteGetFileOrId} },
	{ "delete",	"i",		Command_Delete,	{CompleteGetFileOrId} },
	{ NULL, 	NULL,		Command_Unknown, {} } 
};

const char *alias[] = {"ls", 0};
const int aliasIs[] = {Command_List, 0};

BackupQueries::ParsedCommand::ParsedCommand(const std::string& Command,
	bool isFromCommandLine)
: mInOptions(false),
  mFailed(false),
  pSpec(NULL),
  mCompleteArgCount(0)
{
	mCompleteCommand = Command;
	
	// is the command a shell command?
	if(Command[0] == 's' && Command[1] == 'h' && Command[2] == ' ' && Command[3] != '\0')
	{
		// Yes, run shell command
		for(int i = 0; commands[i].type != Command_Unknown; i++)
		{
			if(commands[i].type == Command_sh)
			{
				pSpec = &(commands[i]);
				break;
			}
		}

		mCmdElements[0] = "sh";
		mCmdElements[1] = Command.c_str() + 3;
		return;
	}

	// split command into components
	bool inQuoted = false;
	mInOptions = false;
	
	std::string currentArg;
	for (std::string::const_iterator c = Command.begin();
		c != Command.end(); c++)
	{
		// Terminating char?
		if(*c == ((inQuoted)?'"':' '))
		{
			if(!currentArg.empty())
			{
				mCmdElements.push_back(currentArg);

				// Because we just found a space, and the last
				// word was not options (otherwise currentArg
				// would be empty), we've received a complete
				// command or non-option argument.
				mCompleteArgCount++;
			}

			currentArg.resize(0);
			inQuoted = false;
			mInOptions = false;
		}
		// Start of quoted parameter?
		else if(currentArg.empty() && *c == '"')
		{
			inQuoted = true;
		}
		// Start of options? You can't have options if there's no
		// command before them, so treat the options as a command (which
		// doesn't exist, so it will fail to parse) in that case.
		else if(currentArg.empty() && *c == '-' && !mCmdElements.empty())
		{
			mInOptions = true;
		}
		else if(mInOptions)
		{
			// Option char
			mOptions += *c;
		}
		else
		{
			// Normal string char, part of current arg
			currentArg += *c;
		}
	}
	
	if(!currentArg.empty())
	{
		mCmdElements.push_back(currentArg);
	}

	// If there are no commands then there's nothing to do except return
	if(mCmdElements.empty())
	{
		return;
	}

	// Work out which command it is...
	int cmd = 0;
	while(commands[cmd].name != 0 && 
		mCmdElements[0] != commands[cmd].name)
	{
		cmd++;
	}
	
	if(commands[cmd].name == 0)
	{
		// Check for aliases
		int a;
		for(a = 0; alias[a] != 0; ++a)
		{
			if(mCmdElements[0] == alias[a])
			{
				// Found an alias
				cmd = aliasIs[a];
				break;
			}
		}
	}

	if(commands[cmd].name == 0)
	{
		mFailed = true;
		return;
	}

	pSpec = &(commands[cmd]);
	
	#ifdef WIN32
	if(isFromCommandLine)
	{
		std::string converted;
		
		if(!ConvertEncoding(mCompleteCommand, CP_ACP, converted, 
			GetConsoleCP()))
		{
			BOX_ERROR("Failed to convert encoding");
			mFailed = true;
		}
		
		mCompleteCommand = converted;
		
		for(std::vector<std::string>::iterator 
			i  = mCmdElements.begin();
			i != mCmdElements.end(); i++)
		{
			if(!ConvertEncoding(*i, CP_ACP, converted, 
				GetConsoleCP()))
			{
				BOX_ERROR("Failed to convert encoding");
				mFailed = true;
			}
			
			*i = converted;
		}
	}
	#endif
}

