// --------------------------------------------------------------------------
//
// File
//		Name:    ConfiguredBackupClient.h
//		Purpose: Takes a client Configuration (bbackupd.conf) and
//		         returns a configured BackupProtocol wrapper, ready
//		         to use.
//		Created: 2018/06/05
//
// --------------------------------------------------------------------------

#ifndef CONFIGUREDBACKUPCLIENT_H
#define CONFIGUREDBACKUPCLIENT_H

#include <string>

#include "autogen_BackupProtocol.h"
#include "BackupConstants.h"
#include "BackupStoreConstants.h"
#include "Configuration.h"
#include "TLSContext.h"
#include "TcpNice.h"

class BackupFileSystem;
class BackupStoreContext;
class NiceSocketStream;
class S3Client;

// --------------------------------------------------------------------------
//
// Class
//		Name:    ConfiguredBackupClient
//		Purpose: Common code between all configured backup client
//		         objects, including extra APIs.
//		Created: 2018/06/05
//
// --------------------------------------------------------------------------

class ConfiguredBackupClient : public BackupProtocolWrapper
{
protected:
	std::auto_ptr<Configuration> mapConfig;
	FILE* mpExtendedLogFileHandle;
	bool mTcpNiceMode;

#ifdef ENABLE_TCP_NICE
	NiceSocketStream *mpNice;
#endif

	virtual std::auto_ptr<BackupProtocolLoginConfirmed> Login(int32_t account_number,
		bool read_only, int64_t expected_current_marker_value = ClientStoreMarker::NotKnown);

public:
	ConfiguredBackupClient(const Configuration& rConfig)
	: mapConfig(new Configuration(rConfig)),
	  mpExtendedLogFileHandle(NULL),
	  mTcpNiceMode(rConfig.GetKeyValueBool("TcpNice"))
	{ }

	~ConfiguredBackupClient()
	{
		if(mpExtendedLogFileHandle != NULL)
		{
			fclose(mpExtendedLogFileHandle);
		}
	}

	// Login to store, lock account (if not read-only), verify client store marker
	// (if not ClientStoreMarker::NotKnown), and return the LoginConfirmed message,
	// with its client store marker updated if a new one has been assigned.
	virtual std::auto_ptr<BackupProtocolLoginConfirmed> Login(bool read_only,
		int64_t expected_current_marker_value = ClientStoreMarker::NotKnown) = 0;

	void SetNiceMode(bool enabled);
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreDaemonClient
//		Purpose: A client that connects to a backupstore (bbstored)
//		         server over the network.
//		Created: 2018/06/05
//
// --------------------------------------------------------------------------

class BackupStoreDaemonClient : public ConfiguredBackupClient
{
private:
	TLSContext mTlsContext;
	int32_t mAccountID;

protected:
	using ConfiguredBackupClient::Login;

public:
	BackupStoreDaemonClient(const Configuration& config);

	virtual std::auto_ptr<BackupProtocolLoginConfirmed> Login(bool read_only,
		int64_t expected_current_marker_value = ClientStoreMarker::NotKnown);
};
	
// --------------------------------------------------------------------------
//
// Class
//		Name:    S3BackupClient
//		Purpose: A client that connects to an Amazon S3-compatible
//		         HTTP server over the network.
//		Created: 2018/06/05
//
// --------------------------------------------------------------------------

class S3BackupClient : public ConfiguredBackupClient
{
private:
	std::auto_ptr<Configuration> mapS3Config;
	std::auto_ptr<S3Client> mapS3Client;
	std::auto_ptr<BackupFileSystem> mapFileSystem;
	std::auto_ptr<BackupStoreContext> mapStoreContext;

protected:
	using ConfiguredBackupClient::Login;

public:
	S3BackupClient(const Configuration& config);

	virtual std::auto_ptr<BackupProtocolLoginConfirmed> Login(bool read_only,
		int64_t expected_current_marker_value = ClientStoreMarker::NotKnown);
};

std::auto_ptr<ConfiguredBackupClient> GetConfiguredBackupClient(const Configuration& config);

#endif // CONFIGUREDBACKUPCLIENT_H
