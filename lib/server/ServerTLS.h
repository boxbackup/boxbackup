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

