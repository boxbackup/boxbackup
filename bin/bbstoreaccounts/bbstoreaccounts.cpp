// --------------------------------------------------------------------------
//
// File
//		Name:    bbstoreaccounts
//		Purpose: backup store administration tool
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <limits.h>
#include <stdio.h>

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <sys/types.h>

#include <iostream>

#include "box_getopt.h"
#include "BackupAccountControl.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreInfo.h"
#include "BannerText.h"
#include "BoxPortsAndFiles.h"
#include "HousekeepStoreAccount.h"
#include "MainHelper.h"
#include "RaidFileController.h"
#include "StoreStructure.h"
#include "UnixUser.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#include <cstring>

int PrintUsage()
{
	std::string configFilename = BOX_GET_DEFAULT_BBSTORED_CONFIG_FILE;

	std::cout <<
	BANNER_TEXT("account management utility (bbstoreaccounts)") << "\n"
	"\n"
	"Usage: bbstoreaccounts [-3] [-c config_file] <action> <account_id> [args]\n"
	"Account ID is integer specified in hex, with no 0x prefix.\n"
	"\n"
	"Options:\n"
	"  -3         Amazon S3 mode. Not all commands are supported yet. Use account\n"
	"             name for <account_id>, and bbackupd.conf for <config_file>.\n"
	"  -c         Use an alternate configuration file instead of\n"
	"             " << configFilename << ".\n"
	<< Logging::OptionParser::GetUsageString() <<
	"\n"
	"Commands (and arguments):\n"
	"  create <account> <discnum> <softlimit> <hardlimit>\n"
	"        Creates a RaidFile account with the specified account number, on the\n"
	"        specified RaidFile disc set number (see raidfile.conf for valid set\n"
	"        numbers) with the specified soft and hard limits (in blocks if\n"
	"        suffixed with B, MB with M, GB with G).\n"
	"  info [-m] <account>\n"
	"        Prints information about the specified account including number\n"
	"        of blocks used. The -m option enables machine-readable output.\n"
	"  enabled <accounts> <yes|no>\n"
	"        Sets the account as enabled or disabled for new logins.\n"
	"  setlimit <accounts> <softlimit> <hardlimit>\n"
	"        Changes the limits of the account as specified. Numbers are\n"
	"        interpreted as for the 'create' command (suffixed with B, M or G).\n"
	"  delete <account> [yes]\n"
	"        Deletes the specified account. Prompts for confirmation unless\n"
	"        the optional 'yes' parameter is provided.\n"
	"  check <account> [fix] [quiet]\n"
	"        Checks the specified account for errors. If the 'fix' option is\n"
	"        provided, any errors discovered that can be fixed automatically\n"
	"        will be fixed. If the 'quiet' option is provided, less output is\n"
	"        produced.\n"
	"  name <account> <new name>\n"
	"        Changes the \"name\" of the account to the specified string.\n"
	"        The name is purely cosmetic and intended to make it easier to\n"
	"        identify your accounts.\n"
	"  housekeep <account>\n"
	"        Runs housekeeping immediately on the account. If it cannot be locked,\n"
	"        bbstoreaccounts returns an error status code (1), otherwise success\n"
	"        (0) even if any errors were fixed by housekeeping.\n";
	return 2;
}

int main(int argc, const char *argv[])
{
	MAINHELPER_SETUP_MEMORY_LEAK_EXIT_REPORT("bbstoreaccounts.memleaks",
		"bbstoreaccounts")

	MAINHELPER_START

	Logging::SetProgramName("bbstoreaccounts");

	// Filename for configuration file?
	std::string configFilename = BOX_GET_DEFAULT_BBSTORED_CONFIG_FILE;
	Logging::OptionParser log_level;
	bool machineReadableOutput = false;
	bool amazon_S3_mode = false;

	// See if there's another entry on the command line
	int c;
	std::string options = Logging::OptionParser::GetOptionString() + "3c:m";
	while((c = getopt(argc, (char * const *)argv, options.c_str())) != -1)
	{
		switch(c)
		{
		case '3':
			amazon_S3_mode = true;
			break;

		case 'c':
			// store argument
			configFilename = optarg;
			break;

		case 'm':
			// enable machine readable output
			machineReadableOutput = true;
			break;

		case '?':
			return PrintUsage();
			break;

		default:
			if(log_level.ProcessOption(c) != 0)
			{
				return PrintUsage();
			}
		}
	}

	Logging::FilterConsole(log_level.GetCurrentLevel());
	Logging::FilterSyslog (Log::NOTHING);

	// Adjust arguments
	argc -= optind;
	argv += optind;

	// We should have at least one argument at this point.
	if(argc < 1)
	{
		return PrintUsage();
	}
	std::string command = argv[0];
	argv++;
	argc--;

	// Read in the configuration file
	std::string errs;
	std::auto_ptr<Configuration> config;
	if(amazon_S3_mode)
	{
		// Read a bbackupd configuration file, instead of a bbstored one.
		if(configFilename.empty())
		{
			configFilename = BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE;
		}
		config = Configuration::LoadAndVerify
			(configFilename, &BackupDaemonConfigVerify, errs);
	}
	else
	{
		if(configFilename.empty())
		{
			configFilename = BOX_GET_DEFAULT_BBSTORED_CONFIG_FILE;
		}
		config = Configuration::LoadAndVerify
			(configFilename, &BackupConfigFileVerify, errs);
	}

	if(config.get() == 0 || !errs.empty())
	{
		BOX_ERROR("Invalid configuration file " << configFilename <<
			":" << errs);
	}

	std::auto_ptr<S3BackupAccountControl> apS3Control;
	std::auto_ptr<BackupStoreAccountControl> apStoreControl;
	BackupAccountControl* pControl;

#define STORE_ONLY \
	if(amazon_S3_mode) \
	{ \
		BOX_ERROR("The '" << command << "' command only applies to bbstored " \
			"backends"); \
		return 2; \
	}

	if(amazon_S3_mode)
	{
		apS3Control.reset(new S3BackupAccountControl(*config,
			machineReadableOutput));
		pControl = apS3Control.get();
	}
	else
	{
		// Initialise the raid file controller. Not needed in Amazon S3 mode.
		RaidFileController &rcontroller(RaidFileController::GetController());
		rcontroller.Initialise(config->GetKeyValue("RaidFileConf").c_str());

		// Get the Account ID (in hex without the leading 0x).
		int32_t id;
		if(argc == 0 || ::sscanf(argv[0], "%x", &id) != 1)
		{
			BOX_FATAL("All commands require an account ID, in hex without 0x");
			return PrintUsage();
		}
		argv++;
		argc--;

		apStoreControl.reset(new BackupStoreAccountControl(*config, id,
			machineReadableOutput));
		pControl = apStoreControl.get();
	}

	BackupAccountControl& control(*pControl);

	// Now do the command.
	try
	{
		if(command == "create")
		{
			// which disc?
			int32_t discnum;

			if(amazon_S3_mode)
			{
				if(argc != 3)
				{
					BOX_ERROR("create requires an account name/label, "
						"soft and hard limits.");
					return 2;
				}
			}
			else
			{
				if(argc != 3 || ::sscanf(argv[0], "%d", &discnum) != 1)
				{
					BOX_ERROR("create requires raid file disc number, "
						"soft and hard limits.");
					return 2;
				}
			}

			// Create the account...
			if(amazon_S3_mode)
			{
				int blocksize = apS3Control->GetBlockSize();
				// Decode limits
				int32_t softlimit = pControl->SizeStringToBlocks(argv[1], blocksize);
				int32_t hardlimit = pControl->SizeStringToBlocks(argv[2], blocksize);
				return apS3Control->CreateAccount(argv[0], softlimit, hardlimit);
			}
			else
			{
				int blocksize = apStoreControl->BlockSizeOfDiscSet(discnum);
				// Decode limits
				int32_t softlimit = pControl->SizeStringToBlocks(argv[1], blocksize);
				int32_t hardlimit = pControl->SizeStringToBlocks(argv[2], blocksize);
				return apStoreControl->CreateAccount(discnum, softlimit, hardlimit);
			}
		}
		else if(command == "info")
		{
			// Print information on this account
			return control.PrintAccountInfo();
		}
		else if(command == "enabled")
		{
			// Change the AccountEnabled flag on this account
			if(argc != 1)
			{
				return PrintUsage();
			}

			bool enabled = true;
			std::string enabled_string = argv[0];
			if(enabled_string == "yes")
			{
				enabled = true;
			}
			else if(enabled_string == "no")
			{
				enabled = false;
			}
			else
			{
				return PrintUsage();
			}

			return control.SetAccountEnabled(enabled);
		}
		else if(command == "setlimit")
		{
			// Change the limits on this account
			if(argc != 2)
			{
				BOX_ERROR("setlimit requires soft and hard limits.");
				return 2;
			}

			return control.SetLimit(argv[0], argv[1]);
		}
		else if(command == "name")
		{
			// Change the limits on this account
			if(argc != 1)
			{
				BOX_ERROR("name command requires a new name.");
				return 1;
			}

			return control.SetAccountName(argv[0]);
		}
		else if(command == "delete")
		{
			// Delete an account
			STORE_ONLY;

			bool askForConfirmation = true;
			if(argc >= 1 && (::strcmp(argv[0], "yes") == 0))
			{
				askForConfirmation = false;
			}
			return apStoreControl->DeleteAccount(askForConfirmation);
		}
		else if(command == "check")
		{
			STORE_ONLY;

			bool fixErrors = false;
			bool quiet = false;

			// Look at other options
			for(int o = 0; o < argc; ++o)
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
			return apStoreControl->CheckAccount(fixErrors, quiet);
		}
		else if(command == "housekeep")
		{
			STORE_ONLY;
			return apStoreControl->HousekeepAccountNow();
		}
		else
		{
			BOX_ERROR("Unknown command '" << command << "'.");
			return 2;
		}
	}
	catch(BoxException &e)
	{
		BOX_ERROR("Failed command: " << command << ": " << e.what());
		return 1;
	}

	return 0;

	MAINHELPER_END
}

