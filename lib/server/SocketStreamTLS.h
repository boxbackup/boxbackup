// --------------------------------------------------------------------------
//
// File
//		Name:    SocketStreamTLS.h
//		Purpose: Socket stream encrpyted and authenticated by TLS
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#ifndef SOCKETSTREAMTLS__H
#define SOCKETSTREAMTLS__H

#include <string>

#include "SocketStream.h"

class TLSContext;
#ifndef TLS_CLASS_IMPLEMENTATION_CPP
	class SSL;
	class BIO;
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    SocketStreamTLS
//		Purpose: Socket stream encrpyted and authenticated by TLS
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
class SocketStreamTLS : public SocketStream
{
public:
	SocketStreamTLS();
	SocketStreamTLS(int socket);
	~SocketStreamTLS();
private:
	SocketStreamTLS(const SocketStreamTLS &rToCopy);
public:

	void Open(const TLSContext &rContext, Socket::Type Type,
		const std::string& rName, int Port = 0);
	void Handshake(const TLSContext &rContext, bool IsServer = false);
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void Close();
	virtual void Shutdown(bool Read = true, bool Write = true);

	std::string GetPeerCommonName();

private:
	bool WaitWhenRetryRequired(int SSLErrorCode, int Timeout);

private:
	SSL *mpSSL;
	BIO *mpBIO;
};

#endif // SOCKETSTREAMTLS__H

