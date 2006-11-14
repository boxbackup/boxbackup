// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemonConfigVerify.cpp
//		Purpose: Configuration file definition for bbackupd
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupDaemonConfigVerify.h"
#include "Daemon.h"
#include "BoxPortsAndFiles.h"

#include "MemLeakFindOn.h"


static const ConfigurationVerifyKey backuplocationkeys[] = 
{
	{"ExcludeFile", 0, ConfigTest_MultiValueAllowed, 0},
	{"ExcludeFilesRegex", 0, ConfigTest_MultiValueAllowed, 0},
	{"ExcludeDir", 0, ConfigTest_MultiValueAllowed, 0},
	{"ExcludeDirsRegex", 0, ConfigTest_MultiValueAllowed, 0},
	{"AlwaysIncludeFile", 0, ConfigTest_MultiValueAllowed, 0},
	{"AlwaysIncludeFilesRegex", 0, ConfigTest_MultiValueAllowed, 0},
	{"AlwaysIncludeDir", 0, ConfigTest_MultiValueAllowed, 0},
	{"AlwaysIncludeDirsRegex", 0, ConfigTest_MultiValueAllowed, 0},
	{"Path", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};

static const ConfigurationVerify backuplocations[] = 
{
	{
		"*",
		0,
		backuplocationkeys,
		ConfigTest_LastEntry,
		0
	}
};

static const ConfigurationVerifyKey verifyserverkeys[] = 
{
	DAEMON_VERIFY_SERVER_KEYS
};

static const ConfigurationVerify verifyserver[] = 
{
	{
		"Server",
		0,
		verifyserverkeys,
		ConfigTest_Exists,
		0
	},
	{
		"BackupLocations",
		backuplocations,
		0,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	}
};

static const ConfigurationVerifyKey verifyrootkeys[] = 
{
	{"AccountNumber", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},

	{"UpdateStoreInterval", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"MinimumFileAge", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"MaxUploadWait", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"MaxFileTimeInFuture", "172800", ConfigTest_IsInt, 0},		// file is uploaded if the file is this much in the future (2 days default)

	{"AutomaticBackup", "yes", ConfigTest_IsBool, 0},
	
	{"SyncAllowScript", 0, 0, 0},			// optional script to run to see if the sync should be started now
				// return "now" if it's allowed, or a number of seconds if it's not

	{"MaximumDiffingTime", 0, ConfigTest_IsInt, 0},

	{"FileTrackingSizeThreshold", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"DiffingUploadSizeThreshold", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"StoreHostname", 0, ConfigTest_Exists, 0},
	{"ExtendedLogging",	"no", ConfigTest_IsBool, 0}, // extended log to syslog
	{"ExtendedLogFile",	NULL, 0, 0}, // extended log to a file

	{"CommandSocket", 0, 0, 0},				// not compulsory to have this
	{"KeepAliveTime", 0, ConfigTest_IsInt, 0},				// optional
 	{"StoreObjectInfoFile", 0, 0, 0},				// optional

	{"NotifyScript", 0, 0, 0},				// optional script to run when backup needs attention, eg store full
	
	{"CertificateFile", 0, ConfigTest_Exists, 0},
	{"PrivateKeyFile", 0, ConfigTest_Exists, 0},
	{"TrustedCAsFile", 0, ConfigTest_Exists, 0},
	{"KeysFile", 0, ConfigTest_Exists, 0},
	{"DataDirectory", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};

const ConfigurationVerify BackupDaemonConfigVerify =
{
	"root",
	verifyserver,
	verifyrootkeys,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};
