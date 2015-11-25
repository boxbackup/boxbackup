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
#include "BackupConstants.h"

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

static const ConfigurationVerifyKey verifys3keys[] =
{
	// These values are only required for Amazon S3-compatible stores
	ConfigurationVerifyKey("HostName", ConfigTest_Exists),
	ConfigurationVerifyKey("Port", ConfigTest_Exists | ConfigTest_IsInt, 80),
	ConfigurationVerifyKey("BasePath", ConfigTest_Exists),
	ConfigurationVerifyKey("AccessKey", ConfigTest_Exists),
	ConfigurationVerifyKey("SecretKey", ConfigTest_Exists | ConfigTest_LastEntry)
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
		"S3Store",
		0,
		verifys3keys,
		0,
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
	ConfigurationVerifyKey("UpdateStoreInterval",
		ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("BackupErrorDelay",
		ConfigTest_IsInt, BACKUP_ERROR_RETRY_SECONDS),
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
	ConfigurationVerifyKey("ExtendedLogging", ConfigTest_IsBool, false),
	// extended log to syslog
	ConfigurationVerifyKey("ExtendedLogFile", 0),
	// extended log to a file
	ConfigurationVerifyKey("LogAllFileAccess", ConfigTest_IsBool, false),
	// enable logging reasons why each file is backed up or not
	ConfigurationVerifyKey("LogFile", 0),
	// enable logging to a file
	ConfigurationVerifyKey("LogFileLevel", 0),
	// set the level of verbosity of file logging
	ConfigurationVerifyKey("LogFileOverwrite", ConfigTest_IsBool, false),
    // set the number of sync stats to keep in memory
    ConfigurationVerifyKey("StatsHistoryLength", ConfigTest_IsUint32, 1),
	// overwrite the log file on each backup
	ConfigurationVerifyKey("CommandSocket", 0),
	// not compulsory to have this
	ConfigurationVerifyKey("KeepAliveTime", ConfigTest_IsInt),
 	ConfigurationVerifyKey("StoreObjectInfoFile", 0),
	// optional

	ConfigurationVerifyKey("NotifyScript", 0),
	// optional script to run when backup needs attention, eg store full
	
	ConfigurationVerifyKey("NotifyAlways", ConfigTest_IsBool, false),
	// option to disable the suppression of duplicate notifications

	ConfigurationVerifyKey("MaxUploadRate", ConfigTest_IsInt),
	// optional maximum speed of uploads in kbytes per second

	ConfigurationVerifyKey("TcpNice", ConfigTest_IsBool, false),
	// optional enable of tcp nice/background mode

	ConfigurationVerifyKey("KeysFile", ConfigTest_Exists),
	ConfigurationVerifyKey("DataDirectory", ConfigTest_Exists),

	// These values are only required for bbstored stores:
	ConfigurationVerifyKey("StoreHostname", 0),
	ConfigurationVerifyKey("StorePort", ConfigTest_IsInt,
		BOX_PORT_BBSTORED),
	ConfigurationVerifyKey("AccountNumber",
		ConfigTest_IsUint32),
	ConfigurationVerifyKey("CertificateFile", 0),
	ConfigurationVerifyKey("PrivateKeyFile", 0),
	ConfigurationVerifyKey("TrustedCAsFile", ConfigTest_LastEntry),
};

const ConfigurationVerify BackupDaemonConfigVerify =
{
	"root",
	verifyserver,
	verifyrootkeys,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};
