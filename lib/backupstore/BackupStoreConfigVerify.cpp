// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreConfigVerify.h
//		Purpose: Configuration definition for the backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupStoreConfigVerify.h"
#include "ServerTLS.h"
#include "BoxPortsAndFiles.h"

#include "MemLeakFindOn.h"

static const ConfigurationVerifyKey verifyserverkeys[] = 
{
	SERVERTLS_VERIFY_SERVER_KEYS(0)	// no default listen addresses
};

static const ConfigurationVerify verifyserver[] = 
{
	{
		"Server",
		0,
		verifyserverkeys,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	}
};

static const ConfigurationVerifyKey verifyrootkeys[] = 
{
	{"AccountDatabase",	0, ConfigTest_Exists, 0},
	{"TimeBetweenHousekeeping",	0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"ExtendedLogging",	"no", ConfigTest_IsBool, 0},			// make value "yes" to enable in config file
	{"RaidFileConf",	BOX_FILE_RAIDFILE_DEFAULT_CONFIG, ConfigTest_LastEntry, 	0}
};

const ConfigurationVerify BackupConfigFileVerify =
{
	"root",
	verifyserver,
	verifyrootkeys,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};
