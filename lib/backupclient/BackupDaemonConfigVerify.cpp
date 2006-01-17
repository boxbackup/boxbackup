// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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
	{"ExtendedLogging",	"no", ConfigTest_IsBool, 0},			// make value "yes" to enable in config file

	{"CommandSocket", 0, 0, 0},				// not compulsory to have this
	{"StoreObjectInfoFile", 0, 0, 0},				// optional
	{"KeepAliveTime", 0, ConfigTest_IsInt, 0},				// optional

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

