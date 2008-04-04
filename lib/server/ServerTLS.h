// --------------------------------------------------------------------------
//
// File
//		Name:    ServerTLS.h
//		Purpose: Implementation of a server using TLS streams
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#ifndef SERVERTLS__H
#define SERVERTLS__H

#include "ServerStream.h"
#include "SocketStreamTLS.h"
#include "SSLLib.h"
#include "TLSContext.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ServerTLS
//		Purpose: Implementation of a server using TLS streams
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
template<int Port, int ListenBacklog = 128, bool ForkToHandleRequests = true>
class ServerTLS : public ServerStream<SocketStreamTLS, Port, ListenBacklog, ForkToHandleRequests>
{
public:
	ServerTLS()
	{
		// Safe to call this here, as the Daemon class makes sure there is only one instance every of a Daemon.
		SSLLib::Initialise();
	}
	
	~ServerTLS()
	{
	}
private:
	ServerTLS(const ServerTLS &)
	{
	}
public:

	virtual void Run2(bool &rChildExit)
	{
		// First, set up the SSL context.
		// Get parameters from the configuration
		// this-> in next line required to build under some gcc versions
		const Configuration &conf(this->GetConfiguration());
		const Configuration &serverconf(conf.GetSubConfiguration("Server"));
		std::string certFile(serverconf.GetKeyValue("CertificateFile"));
		std::string keyFile(serverconf.GetKeyValue("PrivateKeyFile"));
		std::string caFile(serverconf.GetKeyValue("TrustedCAsFile"));
		mContext.Initialise(true /* as server */, certFile.c_str(), keyFile.c_str(), caFile.c_str());
	
		// Then do normal stream server stuff
		ServerStream<SocketStreamTLS, Port, ListenBacklog>::Run2(rChildExit);
	}
	
	virtual void HandleConnection(SocketStreamTLS &rStream)
	{
		rStream.Handshake(mContext, true /* is server */);
		// this-> in next line required to build under some gcc versions
		this->Connection(rStream);
	}
	
private:
	TLSContext mContext;
};

#define SERVERTLS_VERIFY_SERVER_KEYS(DEFAULT_ADDRESSES) \
											{"CertificateFile", 0, ConfigTest_Exists, 0}, \
											{"PrivateKeyFile", 0, ConfigTest_Exists, 0}, \
											{"TrustedCAsFile", 0, ConfigTest_Exists, 0}, \
											SERVERSTREAM_VERIFY_SERVER_KEYS(DEFAULT_ADDRESSES)


#endif // SERVERTLS__H

