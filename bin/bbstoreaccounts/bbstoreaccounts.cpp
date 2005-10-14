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
		printf("ERROR: Soft limit must be less than the hard limit.\n");
		exit(1);
	}
	if(SoftLimit > ((HardLimit * MAX_SOFT_LIMIT_SIZE) / 100))
	{
		printf("ERROR: Soft limit must be no more than %d%% of the hard limit.\n", MAX_SOFT_LIMIT_SIZE);
		exit(1);
	}
}

int BlockSizeOfDiscSet(int DiscSet)
{
	// Get controller, check disc set number
	RaidFileController &controller(RaidFileController::GetController());
	if(DiscSet < 0 || DiscSet >= controller.GetNumDiscSets())
	{
		printf("Disc set %d does not exist\n", DiscSet);
		exit(1);
	}
	
	// Return block size
	return controller.GetDiscSet(DiscSet).GetBlockSize();
}

const char *BlockSizeToString(int64_t Blocks, int DiscSet)
{
	// Not reentrant, nor can be used in the same function call twice, etc.
	static char string[256];
	
	// Work out size in Mb.
	double mb = (Blocks * BlockSizeOfDiscSet(DiscSet)) / (1024.0*1024.0);
	
	// Format string
	sprintf(string, "%lld (%.2fMb)", Blocks, mb);
	
	return string;
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
		printf("%s is an invalid number\n", string);
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
		printf("%s has an invalid units specifier\nUse B for blocks, M for Mb, G for Gb, eg 2Gb\n", string);
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
		printf("Couldn't lock the account -- did not change the limits\nTry again later.\n");
		return 1;
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
		printf("Account %x does not exist\n", ID);
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

	printf("Limits on account 0x%08x changed to %lld soft, %lld hard\n", ID, softlimit, hardlimit);

	return 0;
}

int AccountInfo(Configuration &rConfig, int32_t ID)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		printf("Account %x does not exist\n", ID);
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
	printf("                 Blocks used: %s\n", BlockSizeToString(info->GetBlocksUsed(), discSet));
	printf("    Blocks used by old files: %s\n", BlockSizeToString(info->GetBlocksInOldFiles(), discSet));
	printf("Blocks used by deleted files: %s\n", BlockSizeToString(info->GetBlocksInDeletedFiles(), discSet));
	printf("  Blocks used by directories: %s\n", BlockSizeToString(info->GetBlocksInDirectories(), discSet));
	printf("            Block soft limit: %s\n", BlockSizeToString(info->GetBlocksSoftLimit(), discSet));
	printf("            Block hard limit: %s\n", BlockSizeToString(info->GetBlocksHardLimit(), discSet));
	printf("         Client store marker: %lld\n", info->GetClientStoreMarker());
	
	return 0;
}

int DeleteAccount(Configuration &rConfig, const std::string &rUsername, int32_t ID, bool AskForConfirmation)
{
	// Check user really wants to do this
	if(AskForConfirmation)
	{
		::printf("Really delete account %08x?\n(type 'yes' to confirm)\n", ID);
		char response[256];
		if(::fgets(response, sizeof(response), stdin) == 0 || ::strcmp(response, "yes\n") != 0)
		{
			printf("Deletion cancelled\n");
			return 0;
		}
	}
	
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		printf("Account %x does not exist\n", ID);
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
	
	// Thirdly, delete the directories...
	for(std::vector<std::string>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
	{
		::printf("Deleting store directory %s...\n", (*d).c_str());
		// Just use the rm command to delete the files
		std::string cmd("rm -rf ");
		cmd += *d;
		// Run command
		if(::system(cmd.c_str()) != 0)
		{
			::printf("ERROR: Deletion of %s failed.\n(when cleaning up, remember to delete all raid directories)\n", (*d).c_str());
			return 1;
		}
	}
	
	// Success!
	return 0;
}

int CheckAccount(Configuration &rConfig, const std::string &rUsername, int32_t ID, bool FixErrors, bool Quiet)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(BackupStoreAccountDatabase::Read(rConfig.GetKeyValue("AccountDatabase").c_str()));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		printf("Account %x does not exist\n", ID);
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
		printf("Account %x already exists\n", ID);
		return 1;
	}
	
	// Create it.
	BackupStoreAccounts acc(*db);
	acc.Create(ID, DiscNumber, SoftLimit, HardLimit, rUsername);
	
	printf("Account %x created\n", ID);

	return 0;
}

void PrintUsageAndExit()
{
	printf("Usage: bbstoreaccounts [-c config_file] action account_id [args]\nAccount ID is integer specified in hex\n");
	exit(1);
}

int main(int argc, const char *argv[])
{
	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbstoreaccounts.memleaks", "bbstoreaccounts")

	MAINHELPER_START

	// Filename for configuraiton file?
	const char *configFilename = BOX_FILE_BBSTORED_DEFAULT_CONFIG;
	
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
	std::auto_ptr<Configuration> config(Configuration::LoadAndVerify(configFilename, &BackupConfigFileVerify, errs));
	if(config.get() == 0 || !errs.empty())
	{
		printf("Invalid configuration file:\n%s", errs.c_str());
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
			printf("create requires raid file disc number, soft and hard limits\n");
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
			printf("setlimit requires soft and hard limits\n");
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
				::printf("Unknown option %s.\n", argv[o]);
				return 2;
			}
		}
	
		// Check the account
		return CheckAccount(*config, username, id, fixErrors, quiet);
	}
	else
	{
		printf("Unknown command '%s'\n", argv[0]);
		return 1;
	}

	return 0;
	
	MAINHELPER_END
}


