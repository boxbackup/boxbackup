// --------------------------------------------------------------------------
//
// File
//		Name:    bbstoreaccounts
//		Purpose: backup store administration tool
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <limits.h>
#include <vector>
#include <algorithm>

#include "BoxPortsAndFiles.h"
#include "BackupStoreConfigVerify.h"
#include "RaidFileController.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "MainHelper.h"
#include "BackupStoreInfo.h"
#include "StoreStructure.h"
#include "NamedLock.h"
#include "UnixUser.h"
#include "BackupStoreCheck.h"

#include "MemLeakFindOn.h"

// max size of soft limit as percent of hard limit
#define MAX_SOFT_LIMIT_SIZE		97

void CheckSoftHardLimits(int64_t SoftLimit, int64_t HardLimit)
{
	if(SoftLimit >= HardLimit)
	{
		BOX_FATAL("Soft limit must be less than the hard limit.");
		exit(1);
	}
	if(SoftLimit > ((HardLimit * MAX_SOFT_LIMIT_SIZE) / 100))
	{
		BOX_FATAL("Soft limit must be no more than " << 
			MAX_SOFT_LIMIT_SIZE << "% of the hard limit.");
		exit(1);
	}
}

int BlockSizeOfDiscSet(int DiscSet)
{
	// Get controller, check disc set number
	RaidFileController &controller(RaidFileController::GetController());
	if(DiscSet < 0 || DiscSet >= controller.GetNumDiscSets())
	{
		BOX_FATAL("Disc set " << DiscSet << " does not exist.");
		exit(1);
	}
	
	// Return block size
	return controller.GetDiscSet(DiscSet).GetBlockSize();
}

std::string BlockSizeToString(int64_t Blocks, int DiscSet)
{
	// Work out size in Mb.
	double mb = (Blocks * BlockSizeOfDiscSet(DiscSet)) / (1024.0*1024.0);
	
	// Format string
	std::ostringstream buf;
	buf << Blocks << " blocks " << std::fixed << std::setprecision(2) <<
		std::showpoint << "(" << mb << " MB)";
	return buf.str();
}

int64_t SizeStringToBlocks(const char *string, int DiscSet)
{
	// Find block size
	int blockSize = BlockSizeOfDiscSet(DiscSet);
	
	// Get number
	char *endptr = (char*)string;
	int64_t number = strtol(string, &endptr, 0);
	if(endptr == string || number == LONG_MIN || number == LONG_MAX)
	{
		BOX_FATAL("'" << string << "' is not a valid number.");
		exit(1);
	}
	
	// Check units
	switch(*endptr)
	{
	case 'M':
	case 'm':
		// Units: Mb
		return (number * 1024*1024) / blockSize;
		break;
		
	case 'G':
	case 'g':
		// Units: Gb
		return (number * 1024*1024*1024) / blockSize;
		break;
		
	case 'B':
	case 'b':
		// Units: Blocks
		// Easy! Just return the number specified.
		return number;
		break;
	
	default:
		BOX_FATAL(string << " has an invalid units specifier "
			"(use B for blocks, M for Mb, G for Gb, eg 2Gb)");
		exit(1);
		break;		
	}
}

bool GetWriteLockOnAccount(NamedLock &rLock, const std::string rRootDir, int DiscSetNum)
{
	std::string writeLockFilename;
	StoreStructure::MakeWriteLockFilename(rRootDir, DiscSetNum, writeLockFilename);

	bool gotLock = false;
	int triesLeft = 8;
	do
	{
		gotLock = rLock.TryAndGetLock(writeLockFilename.c_str(), 0600 /* restrictive file permissions */);
		
		if(!gotLock)
		{
			--triesLeft;
			::sleep(1);
		}
	} while(!gotLock && triesLeft > 0);

	if(!gotLock)
	{
		// Couldn't lock the account -- just stop now
		BOX_ERROR("Failed to lock the account, did not change limits. "
			"Try again later.");
	}

	return gotLock;
}

int SetLimit(Configuration &rConfig, const std::string &rUsername, int32_t ID, const char *SoftLimitStr, const char *HardLimitStr)
{
	// Become the user specified in the config file?
	std::auto_ptr<UnixUser> user;
	if(!rUsername.empty())
	{
		// Username specified, change...
		user.reset(new UnixUser(rUsername.c_str()));
		user->ChangeProcessUser(true /* temporary */);
		// Change will be undone at the end of this function
	}

	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Already exists?
	if(!db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" does not exist.");
		return 1;
	}
	
	// Load it in
	BackupStoreAccounts acc(*db);
	std::string rootDir;
	int discSet;
	acc.GetAccountRoot(ID, rootDir, discSet);
	
	// Attempt to lock
	NamedLock writeLock;
	if(!GetWriteLockOnAccount(writeLock, rootDir, discSet))
	{
		// Failed to get lock
		return 1;
	}

	// Load the info
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID, rootDir, discSet, false /* Read/Write */));

	// Change the limits
	int64_t softlimit = SizeStringToBlocks(SoftLimitStr, discSet);
	int64_t hardlimit = SizeStringToBlocks(HardLimitStr, discSet);
	CheckSoftHardLimits(softlimit, hardlimit);
	info->ChangeLimits(softlimit, hardlimit);
	
	// Save
	info->Save();

	BOX_NOTICE("Limits on account " << BOX_FORMAT_ACCOUNT(ID) <<
		" changed to " << softlimit << " soft, " <<
		hardlimit << " hard.");

	return 0;
}

int AccountInfo(Configuration &rConfig, int32_t ID)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" does not exist.");
		return 1;
	}
	
	// Load it in
	BackupStoreAccounts acc(*db);
	std::string rootDir;
	int discSet;
	acc.GetAccountRoot(ID, rootDir, discSet);
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID, rootDir, discSet, true /* ReadOnly */));
	
	// Then print out lots of info
	printf("                  Account ID: %08x\n", ID);
	printf("              Last object ID: %lld\n", info->GetLastObjectIDUsed());
	printf("                 Blocks used: %s\n", BlockSizeToString(info->GetBlocksUsed(), discSet).c_str());
	printf("    Blocks used by old files: %s\n", BlockSizeToString(info->GetBlocksInOldFiles(), discSet).c_str());
	printf("Blocks used by deleted files: %s\n", BlockSizeToString(info->GetBlocksInDeletedFiles(), discSet).c_str());
	printf("  Blocks used by directories: %s\n", BlockSizeToString(info->GetBlocksInDirectories(), discSet).c_str());
	printf("            Block soft limit: %s\n", BlockSizeToString(info->GetBlocksSoftLimit(), discSet).c_str());
	printf("            Block hard limit: %s\n", BlockSizeToString(info->GetBlocksHardLimit(), discSet).c_str());
	printf("         Client store marker: %lld\n", info->GetClientStoreMarker());
	
	return 0;
}

int DeleteAccount(Configuration &rConfig, const std::string &rUsername, int32_t ID, bool AskForConfirmation)
{
	// Check user really wants to do this
	if(AskForConfirmation)
	{
		BOX_WARNING("Really delete account " << 
			BOX_FORMAT_ACCOUNT(ID) << "? (type 'yes' to confirm)");
		char response[256];
		if(::fgets(response, sizeof(response), stdin) == 0 || ::strcmp(response, "yes\n") != 0)
		{
			BOX_NOTICE("Deletion cancelled.");
			return 0;
		}
	}
	
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" does not exist.");
		return 1;
	}
	
	// Get info from the database
	BackupStoreAccounts acc(*db);
	std::string rootDir;
	int discSetNum;
	acc.GetAccountRoot(ID, rootDir, discSetNum);
	
	// Obtain a write lock, as the daemon user
	NamedLock writeLock;
	{
		// Bbecome the user specified in the config file
		std::auto_ptr<UnixUser> user;
		if(!rUsername.empty())
		{
			// Username specified, change...
			user.reset(new UnixUser(rUsername.c_str()));
			user->ChangeProcessUser(true /* temporary */);
			// Change will be undone at the end of this function
		}
	
		// Get a write lock
		if(!GetWriteLockOnAccount(writeLock, rootDir, discSetNum))
		{
			// Failed to get lock
			return 1;
		}
		
		// Back to original user, but write is maintained
	}

	// Delete from account database
	db->DeleteEntry(ID);
	
	// Write back to disc
	db->Write();
	
	// Remove the store files...

	// First, become the user specified in the config file
	std::auto_ptr<UnixUser> user;
	if(!rUsername.empty())
	{
		// Username specified, change...
		user.reset(new UnixUser(rUsername.c_str()));
		user->ChangeProcessUser(true /* temporary */);
		// Change will be undone at the end of this function
	}
	
	// Secondly, work out which directories need wiping
	std::vector<std::string> toDelete;
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet discSet(rcontroller.GetDiscSet(discSetNum));
	for(RaidFileDiscSet::const_iterator i(discSet.begin()); i != discSet.end(); ++i)
	{
		if(std::find(toDelete.begin(), toDelete.end(), *i) == toDelete.end())
		{
			toDelete.push_back((*i) + DIRECTORY_SEPARATOR + rootDir);
		}
	}

	int retcode = 0;

	// Thirdly, delete the directories...
	for(std::vector<std::string>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
	{
		BOX_NOTICE("Deleting store directory " << (*d) << "...");
		// Just use the rm command to delete the files
		std::string cmd("rm -rf ");
		cmd += *d;
		// Run command
		if(::system(cmd.c_str()) != 0)
		{
			BOX_ERROR("Failed to delete files in " << (*d) <<
				", delete them manually.");
			retcode = 1;
		}
	}
	
	// Success!
	return retcode;
}

int CheckAccount(Configuration &rConfig, const std::string &rUsername, int32_t ID, bool FixErrors, bool Quiet)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" does not exist.");
		return 1;
	}
	
	// Get info from the database
	BackupStoreAccounts acc(*db);
	std::string rootDir;
	int discSetNum;
	acc.GetAccountRoot(ID, rootDir, discSetNum);
	
	// Become the right user
	std::auto_ptr<UnixUser> user;
	if(!rUsername.empty())
	{
		// Username specified, change...
		user.reset(new UnixUser(rUsername.c_str()));
		user->ChangeProcessUser(true /* temporary */);
		// Change will be undone at the end of this function
	}

	// Check it
	BackupStoreCheck check(rootDir, discSetNum, ID, FixErrors, Quiet);
	check.Check();
	
	return check.ErrorsFound()?1:0;
}

int CreateAccount(Configuration &rConfig, const std::string &rUsername, int32_t ID, int32_t DiscNumber, int32_t SoftLimit, int32_t HardLimit)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Already exists?
	if(db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" already exists.");
		return 1;
	}
	
	// Create it.
	BackupStoreAccounts acc(*db);
	acc.Create(ID, DiscNumber, SoftLimit, HardLimit, rUsername);
	
	BOX_NOTICE("Account " << BOX_FORMAT_ACCOUNT(ID) << " created.");

	return 0;
}

void PrintUsageAndExit()
{
	printf("Usage: bbstoreaccounts [-c config_file] action account_id [args]\nAccount ID is integer specified in hex\n");
	exit(1);
}

int main(int argc, const char *argv[])
{
	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbstoreaccounts.memleaks",
		"bbstoreaccounts")

	MAINHELPER_START

	// Filename for configuration file?
	std::string configFilename;

	#ifdef WIN32
		configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
	#else
		configFilename = BOX_FILE_BBSTORED_DEFAULT_CONFIG;
	#endif
	
	// See if there's another entry on the command line
	int c;
	while((c = getopt(argc, (char * const *)argv, "c:")) != -1)
	{
		switch(c)
		{
		case 'c':
			// store argument
			configFilename = optarg;
			break;
		
		case '?':
		default:
			PrintUsageAndExit();
		}
	}
	// Adjust arguments
	argc -= optind;
	argv += optind;

	// Read in the configuration file
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			(configFilename, &BackupConfigFileVerify, errs));

	if(config.get() == 0 || !errs.empty())
	{
		BOX_ERROR("Invalid configuration file " << configFilename <<
			":" << errs);
	}
	
	// Get the user under which the daemon runs
	std::string username;
	{
		const Configuration &rserverConfig(config->GetSubConfiguration("Server"));
		if(rserverConfig.KeyExists("User"))
		{
			username = rserverConfig.GetKeyValue("User");
		}
	}
	
	// Initialise the raid file controller
	RaidFileController &rcontroller(RaidFileController::GetController());
	rcontroller.Initialise(config->GetKeyValue("RaidFileConf").c_str());

	// Then... check we have two arguments
	if(argc < 2)
	{
		PrintUsageAndExit();
	}
	
	// Get the id
	int32_t id;
	if(::sscanf(argv[1], "%x", &id) != 1)
	{
		PrintUsageAndExit();
	}
	
	// Now do the command.
	if(::strcmp(argv[0], "create") == 0)
	{
		// which disc?
		int32_t discnum;
		int32_t softlimit;
		int32_t hardlimit;
		if(argc < 5
			|| ::sscanf(argv[2], "%d", &discnum) != 1)
		{
			BOX_ERROR("create requires raid file disc number, "
				"soft and hard limits.");
			return 1;
		}
		
		// Decode limits
		softlimit = SizeStringToBlocks(argv[3], discnum);
		hardlimit = SizeStringToBlocks(argv[4], discnum);
		CheckSoftHardLimits(softlimit, hardlimit);
	
		// Create the account...
		return CreateAccount(*config, username, id, discnum, softlimit, hardlimit);
	}
	else if(::strcmp(argv[0], "info") == 0)
	{
		// Print information on this account
		return AccountInfo(*config, id);
	}
	else if(::strcmp(argv[0], "setlimit") == 0)
	{
		// Change the limits on this account
		if(argc < 4)
		{
			BOX_ERROR("setlimit requires soft and hard limits.");
			return 1;
		}
		
		return SetLimit(*config, username, id, argv[2], argv[3]);
	}
	else if(::strcmp(argv[0], "delete") == 0)
	{
		// Delete an account
		bool askForConfirmation = true;
		if(argc >= 3 && (::strcmp(argv[2], "yes") == 0))
		{
			askForConfirmation = false;
		}
		return DeleteAccount(*config, username, id, askForConfirmation);
	}
	else if(::strcmp(argv[0], "check") == 0)
	{
		bool fixErrors = false;
		bool quiet = false;
		
		// Look at other options
		for(int o = 2; o < argc; ++o)
		{
			if(::strcmp(argv[o], "fix") == 0)
			{
				fixErrors = true;
			}
			else if(::strcmp(argv[o], "quiet") == 0)
			{
				quiet = true;
			}
			else
			{
				BOX_ERROR("Unknown option " << argv[o] << ".");
				return 2;
			}
		}
	
		// Check the account
		return CheckAccount(*config, username, id, fixErrors, quiet);
	}
	else
	{
		BOX_ERROR("Unknown command '" << argv[0] << "'.");
		return 1;
	}

	return 0;
	
	MAINHELPER_END
}


