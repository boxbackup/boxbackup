// --------------------------------------------------------------------------
//
// File
//		Name:    ConfiguredBackupClient.cpp
//		Purpose: Takes a client Configuration (bbackupd.conf) and
//		         returns a configured BackupProtocol wrapper, ready
//		         to use.
//		Created: 2018/06/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "BackupConstants.h"
#include "BackupFileSystem.h"
#include "BackupStoreContext.h"
#include "ConfiguredBackupClient.h"
#include "S3Client.h"
#include "SocketStreamTLS.h"
#include "autogen_ClientException.h"

#include "MemLeakFindOn.h"

BackupStoreDaemonClient::BackupStoreDaemonClient(const Configuration& config)
: ConfiguredBackupClient(config)
{
	std::string certFile(config.GetKeyValue("CertificateFile"));
	std::string keyFile(config.GetKeyValue("PrivateKeyFile"));
	std::string caFile(config.GetKeyValue("TrustedCAsFile"));
	mTlsContext.Initialise(false /* as client */, certFile.c_str(),
		keyFile.c_str(), caFile.c_str());

	std::auto_ptr<SocketStream> apSocket(new SocketStreamTLS);

	const std::string& hostname = config.GetKeyValue("StoreHostname");
	const int port = config.GetKeyValueInt("StorePort");

	// Log intention
	BOX_INFO("Opening connection to server '" << hostname << ":" << port <<
		"'...");

	// Connect!
	((SocketStreamTLS *)(apSocket.get()))->Open(mTlsContext,
		Socket::TypeINET, hostname, port, BACKUP_STORE_TIMEOUT);

	if(config.GetKeyValueBool("TcpNice"))
	{
#ifdef ENABLE_TCP_NICE
		// Pass control of apSocket to NiceSocketStream,
		// which will take care of destroying it for us.
		// But we need to hang onto a pointer to the nice
		// socket, so we can enable and disable nice mode.
		// This is scary, it could be deallocated under us.
		mpNice = new NiceSocketStream(apSocket);
		apSocket.reset(mpNice);
#else
		BOX_WARNING("TcpNice option is enabled but not supported on this system");
#endif
	}

	BackupProtocolClient* pClient = new BackupProtocolClient(apSocket);
	SetImplementation(pClient);
	pClient->Handshake();
	mAccountID = config.GetKeyValueInt("AccountNumber");
}

S3BackupClient::S3BackupClient(const Configuration& config)
: ConfiguredBackupClient(config)
{
	mapS3Config.reset(new Configuration(config.GetSubConfiguration("S3Store")));
	mapS3Client.reset(new S3Client(*mapS3Config));
	mapFileSystem.reset(new S3BackupFileSystem(*mapS3Config, *mapS3Client));
	mapStoreContext.reset(new BackupStoreContext(*mapFileSystem, NULL,
		"S3BackupClient"));
	mapStoreContext->SetClientHasAccount();
	SetImplementation(new BackupProtocolLocal(*mapStoreContext));
}

std::auto_ptr<ConfiguredBackupClient> GetConfiguredBackupClient(const Configuration& config)
{
	const std::string& storage_backend = config.GetKeyValue("StorageBackend");
	std::auto_ptr<ConfiguredBackupClient> apClient;

	if(storage_backend == "bbstored")
	{
		apClient.reset(new BackupStoreDaemonClient(config));
	}
	else if(storage_backend == "s3")
	{
		apClient.reset(new S3BackupClient(config));
	}
	else
	{
		THROW_EXCEPTION_MESSAGE(CommonException, InvalidConfiguration,
			"Unsupported value of StorageBackend: '" << storage_backend << "'");
	}

	return apClient;
}

std::auto_ptr<BackupProtocolLoginConfirmed> ConfiguredBackupClient::Login(int32_t account_number,
	bool read_only, int64_t expected_current_marker_value)
{
	// Set logging option
	SetLogToSysLog(mapConfig->GetKeyValueBool("ExtendedLogging"));

	if(mapConfig->KeyExists("ExtendedLogFile"))
	{
		ASSERT(mpExtendedLogFileHandle == NULL);
		std::string extended_log_file = mapConfig->GetKeyValue("ExtendedLogFile");

		mpExtendedLogFileHandle = fopen(extended_log_file.c_str(), "a+");

		if (!mpExtendedLogFileHandle)
		{
			BOX_LOG_SYS_ERROR(BOX_FILE_MESSAGE(extended_log_file,
				"Failed to open extended log file"));
		}
		else
		{
			SetLogToFile(mpExtendedLogFileHandle);
		}
	}

	// Check the version of the server
	{
		std::auto_ptr<BackupProtocolVersion> serverVersion(
			QueryVersion(BACKUP_STORE_SERVER_VERSION));
		if(serverVersion->GetVersion() != BACKUP_STORE_SERVER_VERSION)
		{
			THROW_EXCEPTION(BackupStoreException, WrongServerVersion)
		}
	}

	// Login -- if this fails, the Protocol will exception
	std::auto_ptr<BackupProtocolLoginConfirmed> ap_login_conf = QueryLogin(account_number, read_only);

	// If reconnecting, check that the client store marker is the one we expect:
	if(expected_current_marker_value != ClientStoreMarker::NotKnown)
	{
		if(ap_login_conf->GetClientStoreMarker() != expected_current_marker_value)
		{
			// Not good... finish the connection, abort, etc, ignoring errors
			try
			{
				QueryFinished();
			}
			catch(...)
			{
				// IGNORE
			}

			// Then throw an exception about this:
			THROW_EXCEPTION_MESSAGE(BackupStoreException, ClientMarkerNotAsExpected,
				"Client store marker unexpectedly changed from " <<
				expected_current_marker_value << " to " <<
				ap_login_conf->GetClientStoreMarker() << ": is someone else "
				"writing to the same account?");
		}
	}

	return ap_login_conf;
}

std::auto_ptr<BackupProtocolLoginConfirmed> BackupStoreDaemonClient::Login(bool read_only,
	int64_t expected_current_marker_value)
{
	return Login(mAccountID, read_only, expected_current_marker_value);
}

std::auto_ptr<BackupProtocolLoginConfirmed> S3BackupClient::Login(bool read_only,
	int64_t expected_current_marker_value)
{
	return Login(S3_FAKE_ACCOUNT_ID, read_only, expected_current_marker_value);
}

void ConfiguredBackupClient::SetNiceMode(bool enabled)
{
	if(mTcpNiceMode)
	{
#ifdef ENABLE_TCP_NICE
		mpNice->SetEnabled(enabled);
#endif
	}
}
