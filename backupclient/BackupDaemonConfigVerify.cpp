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
	ConfigurationVerifyKey("ExcludeFile", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("ExcludeFilesRegex", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("ExcludeDir", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("ExcludeDirsRegex", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("AlwaysIncludeFile", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("AlwaysIncludeFilesRegex", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("AlwaysIncludeDir", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("AlwaysIncludeDirsRegex", ConfigTest_MultiValueAllowed),
	ConfigurationVerifyKey("Path", ConfigTest_Exists | ConfigTest_LastEntry)
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
	ConfigurationVerifyKey("AccountNumber",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("UpdateStoreInterval",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("MinimumFileAge",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("MaxUploadWait",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("MaxFileTimeInFuture", ConfigTest_IsInt, 172800),
	// file is uploaded if the file is this much in the future
	// (2 days default)
	ConfigurationVerifyKey("AutomaticBackup", ConfigTest_IsBool, true),
	
	ConfigurationVerifyKey("SyncAllowScript", 0),
	// script that returns "now" if backup is allowed now, or a number
	// of seconds to wait before trying again if not

	ConfigurationVerifyKey("MaximumDiffingTime", ConfigTest_IsInt),
	ConfigurationVerifyKey("DeleteRedundantLocationsAfter",
		ConfigTest_IsInt, 172800),

	ConfigurationVerifyKey("FileTrackingSizeThreshold", 
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("DiffingUploadSizeThreshold",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("StoreHostname", ConfigTest_Exists),
	ConfigurationVerifyKey("StorePort", ConfigTest_IsInt,
		BOX_PORT_BBSTORED),
	ConfigurationVerifyKey("ExtendedLogging", ConfigTest_IsBool, false),
	// extended log to syslog
	ConfigurationVerifyKey("ExtendedLogFile", 0),
	// extended log to a file
	ConfigurationVerifyKey("LogAllFileAccess", ConfigTest_IsBool, false),
	ConfigurationVerifyKey("CommandSocket", 0),
	// not compulsory to have this
	ConfigurationVerifyKey("KeepAliveTime", ConfigTest_IsInt),
 	ConfigurationVerifyKey("StoreObjectInfoFile", 0),
	// optional

	ConfigurationVerifyKey("NotifyScript", 0),
	// optional script to run when backup needs attention, eg store full
	
	ConfigurationVerifyKey("CertificateFile", ConfigTest_Exists),
	ConfigurationVerifyKey("PrivateKeyFile", ConfigTest_Exists),
	ConfigurationVerifyKey("TrustedCAsFile", ConfigTest_Exists),
	ConfigurationVerifyKey("KeysFile", ConfigTest_Exists),
	ConfigurationVerifyKey("DataDirectory", 
		ConfigTest_Exists | ConfigTest_LastEntry),
};

const ConfigurationVerify BackupDaemonConfigVerify =
{
	"root",
	verifyserver,
	verifyrootkeys,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};
