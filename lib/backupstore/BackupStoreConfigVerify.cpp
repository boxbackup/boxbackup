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
	SERVERTLS_VERIFY_SERVER_KEYS(ConfigurationVerifyKey::NoDefaultValue)
	// no default listen addresses
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
	ConfigurationVerifyKey("AccountDatabase", ConfigTest_Exists),
	ConfigurationVerifyKey("TimeBetweenHousekeeping",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("ExtendedLogging", ConfigTest_IsBool, false),
	// make value "yes" to enable in config file

	#ifdef WIN32
		ConfigurationVerifyKey("RaidFileConf", ConfigTest_LastEntry)
	#else
		ConfigurationVerifyKey("RaidFileConf", ConfigTest_LastEntry,
			BOX_FILE_RAIDFILE_DEFAULT_CONFIG)
	#endif
};

const ConfigurationVerify BackupConfigFileVerify =
{
	"root",
	verifyserver,
	verifyrootkeys,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};
