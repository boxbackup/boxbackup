// --------------------------------------------------------------------------
//
// File
//		Name:    SocketStreamTLS.cpp
//		Purpose: Socket stream encrpyted and authenticated by TLS
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP

#include <errno.h>
#include <fcntl.h>

#ifndef WIN32
#include <poll.h>
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "Protocol.h"
#include "autogen_ConnectionException.h"
#include "autogen_ServerException.h"
#include "BoxTime.h"
#include "CryptoUtils.h"
#include "SocketStreamTLS.h"
#include "SSLLib.h"
#include "TLSContext.h"

#include "MemLeakFindOn.h"

// Allow 5 minutes to handshake (in milliseconds)
#define TLS_HANDSHAKE_TIMEOUT (5*60*1000)

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::SocketStreamTLS()
//		Purpose: Constructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
SocketStreamTLS::SocketStreamTLS()
	: mpSSL(0), mpBIO(0)
{
	ResetCounters();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::SocketStreamTLS(int)
//		Purpose: Constructor, taking previously connected socket
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
SocketStreamTLS::SocketStreamTLS(int socket)
	: SocketStream(socket),
	  mpSSL(0), mpBIO(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::~SocketStreamTLS()
//		Purpose: Destructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
SocketStreamTLS::~SocketStreamTLS()
{
	if(mpSSL)
	{
		// Attempt to close to avoid problems
		Close();
		
		// And if that didn't work...
		if(mpSSL)
		{
			::SSL_free(mpSSL);
			mpSSL = 0;
			mpBIO = 0;	// implicity freed by the SSL_free call
		}
	}
	
	// If we only got to creating that BIO.
	if(mpBIO)
	{
		::BIO_free(mpBIO);
		mpBIO = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Open(const TLSContext &, int, const char *, int)
//		Purpose: Open connection, and perform TLS handshake
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SocketStreamTLS::Open(const TLSContext &rContext, Socket::Type Type,
	const std::string& rName, int Port)
{
	SocketStream::Open(Type, rName, Port);
	Handshake(rContext);
	ResetCounters();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Handshake(const TLSContext &, bool)
//		Purpose: Perform TLS handshake
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SocketStreamTLS::Handshake(const TLSContext &rContext, bool IsServer)
{
	if(mpBIO || mpSSL) {THROW_EXCEPTION(ServerException, TLSAlreadyHandshaked)}

	// Create a BIO for this socket
	mpBIO = ::BIO_new(::BIO_s_socket());
	if(mpBIO == 0)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, TLSAllocationFailed,
			"Failed to create SSL BIO: " << CryptoUtils::LogError("creating socket bio"));
	}

	tOSSocketHandle socket = GetSocketHandle();
	BIO_set_fd(mpBIO, socket, BIO_NOCLOSE);

	// Then the SSL object
	mpSSL = ::SSL_new(rContext.GetRawContext());
	if(mpSSL == 0)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, TLSAllocationFailed,
			"Failed to create SSL object: " << CryptoUtils::LogError("creating SSL object"));
	}

	// Make the socket non-blocking so timeouts on Read work

#ifdef WIN32
	u_long nonblocking = 1;
	ioctlsocket(socket, FIONBIO, &nonblocking);
#else // !WIN32
	// This is more portable than using ioctl with FIONBIO
	int statusFlags = 0;
	if(::fcntl(socket, F_GETFL, &statusFlags) < 0
	   || ::fcntl(socket, F_SETFL, statusFlags | O_NONBLOCK) == -1)
	{
		THROW_EXCEPTION(ServerException, SocketSetNonBlockingFailed)
	}
#endif

	// FIXME: This is less portable than the above. However, it MAY be needed
	// for cygwin, which has/had bugs with fcntl
	//
	// int nonblocking = true;
	// if(::ioctl(socket, FIONBIO, &nonblocking) == -1)
	// {
	// 	THROW_EXCEPTION(ServerException, SocketSetNonBlockingFailed)
	// }

	// Set the two to know about each other
	::SSL_set_bio(mpSSL, mpBIO, mpBIO);

	bool waitingForHandshake = true;
	while(waitingForHandshake)
	{
		// Attempt to do the handshake
		int r = 0;
		if(IsServer)
		{
			r = ::SSL_accept(mpSSL);
		}
		else
		{
			r = ::SSL_connect(mpSSL);
		}

		// check return code
		int se;
		switch((se = ::SSL_get_error(mpSSL, r)))
		{
		case SSL_ERROR_NONE:
			// No error, handshake succeeded
			waitingForHandshake = false;
			break;

		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
			// wait for the requried data
			if(WaitWhenRetryRequired(se, TLS_HANDSHAKE_TIMEOUT) == false)
			{
				// timed out
				THROW_EXCEPTION(ConnectionException, TLSHandshakeTimedOut)
			}
			break;
			
		default: // (and SSL_ERROR_ZERO_RETURN)
			// Error occured
#if HAVE_DECL_SSL_R_EE_KEY_TOO_SMALL
			int err_reason = ERR_GET_REASON(ERR_peek_error());
			const char *file, *data;
			int line, flags;
			ERR_peek_error_line_data(&file, &line, &data, &flags);
			long verify_result = SSL_get_verify_result(mpSSL);

			if(se == SSL_ERROR_SSL && verify_result == X509_V_ERR_CA_KEY_TOO_SMALL)
			{
				// Would be nice to use GetPeerCommonName() in these error messages,
				// but since the certificate isn't trusted, that might be misleading,
				// and it's not available to us anyway :(

				THROW_EXCEPTION_MESSAGE(ConnectionException, TLSPeerWeakCertificate,
					(IsServer ? "Failed to accept connection from" :
						"Failed to connect to") << " " << mPeerSocketDesc <<
					": key too short for current security level");
			}
			else if(se == SSL_ERROR_SSL && verify_result == X509_V_ERR_CA_MD_TOO_WEAK)
			{
				THROW_EXCEPTION_MESSAGE(ConnectionException, TLSPeerWeakCertificate,
					(IsServer ? "Failed to accept connection from" :
						"Failed to connect to") << " " << mPeerSocketDesc <<
					": hash too weak for current security level");
			}
			else
#endif // HAVE_DECL_SSL_R_EE_KEY_TOO_SMALL
			if(IsServer)
			{
				THROW_EXCEPTION_MESSAGE(ConnectionException, TLSHandshakeFailed,
					"Failed to accept connection: " <<
					CryptoUtils::LogError("accepting connection"));
			}
			else
			{
				THROW_EXCEPTION_MESSAGE(ConnectionException, TLSHandshakeFailed,
					"Failed to connect: " << CryptoUtils::LogError("connecting"));
			}
		}
	}
	
	// And that's it
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WaitWhenRetryRequired(int, int)
//		Purpose: Waits until the condition required by the TLS layer
//		         is met. Returns true if the condition is met, false
//		         if timed out.
//		Created: 2003/08/15
//
// --------------------------------------------------------------------------
bool SocketStreamTLS::WaitWhenRetryRequired(int SSLErrorCode, int Timeout)
{
	CheckForMissingTimeout(Timeout);

	short events;
	switch(SSLErrorCode)
	{
	case SSL_ERROR_WANT_READ:
		events = POLLIN;
		break;
		
	case SSL_ERROR_WANT_WRITE:
		events = POLLOUT;
		break;

	default:
		// Not good!
		THROW_EXCEPTION(ServerException, Internal)
		break;
	}

	return Poll(events, Timeout);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Read(void *, int, int Timeout)
//		Purpose: See base class
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
int SocketStreamTLS::Read(void *pBuffer, int NBytes, int Timeout)
{
	CheckForMissingTimeout(Timeout);
	if(!mpSSL) {THROW_EXCEPTION(ServerException, TLSNoSSLObject)}

	// Make sure zero byte reads work as expected
	if(NBytes == 0)
	{
		return 0;
	}

	// Make sure we always have a timeout set
	// Deadlock may occur if we don't
	if(Timeout == IOStream::TimeOutInfinite) {
		Timeout = PROTOCOL_DEFAULT_TIMEOUT;
	}

	while(true)
	{
		int r = ::SSL_read(mpSSL, pBuffer, NBytes);

		int se;
		switch((se = ::SSL_get_error(mpSSL, r)))
		{
		case SSL_ERROR_NONE:
			// No error, return number of bytes read
			mBytesRead += r;
			return r;
			break;

		case SSL_ERROR_ZERO_RETURN:
			// Connection closed
			MarkAsReadClosed();
			return 0;
			break;

		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
			// wait for the required data
			// Will only get once around this loop, so don't need to calculate timeout values
			if(WaitWhenRetryRequired(se, Timeout) == false)
			{
				// timed out
				return 0;
			}
			break;
			
		default:
			CryptoUtils::LogError("reading");
			THROW_EXCEPTION(ConnectionException, TLSReadFailed)
			break;
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Write(const void *, int)
//		Purpose: See base class
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SocketStreamTLS::Write(const void *pBuffer, int NBytes, int Timeout)
{
	if(!mpSSL) {THROW_EXCEPTION(ServerException, TLSNoSSLObject)}

	// Make sure zero byte writes work as expected
	if(NBytes == 0)
	{
		return;
	}

	// Make sure we always have a timeout set
	// Deadlock may occur if we don't
	if(Timeout == IOStream::TimeOutInfinite) {
		Timeout = PROTOCOL_DEFAULT_TIMEOUT;
	}

	// from man SSL_write
	//
	// SSL_write() will only return with success, when the
	// complete contents of buf of length num has been written.
	//
	// So no worries about partial writes and moving the buffer around

	while(true)
	{
		// try the write
		int r = ::SSL_write(mpSSL, pBuffer, NBytes);
		
		int se;
		switch((se = ::SSL_get_error(mpSSL, r)))
		{
		case SSL_ERROR_NONE:
			// No error, data sent, return success
			mBytesWritten += r;
			return;
			break;

		case SSL_ERROR_ZERO_RETURN:
			// Connection closed
			MarkAsWriteClosed();
			THROW_EXCEPTION(ConnectionException, TLSClosedWhenWriting);
			break;

		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
			// Wait until we can read or write the socket again, or timeout:
			if(!WaitWhenRetryRequired(se, Timeout))
			{
				THROW_EXCEPTION(ConnectionException, Protocol_Timeout);
			}
			break;
		
		default:
			CryptoUtils::LogError("writing");
			THROW_EXCEPTION(ConnectionException, TLSWriteFailed);
			break;
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Close()
//		Purpose: See base class
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SocketStreamTLS::Close()
{
	if(!mpSSL) {THROW_EXCEPTION(ServerException, TLSNoSSLObject)}

	// Base class to close
	SocketStream::Close();

	// Free resources
	::SSL_free(mpSSL);
	mpSSL = 0;
	mpBIO = 0;	// implicitly freed by SSL_free
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::Shutdown()
//		Purpose: See base class
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void SocketStreamTLS::Shutdown(bool Read, bool Write)
{
	if(!mpSSL) {THROW_EXCEPTION(ServerException, TLSNoSSLObject)}

	if(::SSL_shutdown(mpSSL) < 0)
	{
		CryptoUtils::LogError("shutting down");
		THROW_EXCEPTION(ConnectionException, TLSShutdownFailed)
	}

	// Don't ask the base class to shutdown -- BIO does this, apparently.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStreamTLS::GetPeerCommonName()
//		Purpose: Returns the common name of the other end of the connection
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
std::string SocketStreamTLS::GetPeerCommonName()
{
	if(!mpSSL) {THROW_EXCEPTION(ServerException, TLSNoSSLObject)}

	// Get certificate
	X509 *cert = ::SSL_get_peer_certificate(mpSSL);
	if(cert == 0)
	{
		::X509_free(cert);
		THROW_EXCEPTION(ConnectionException, TLSNoPeerCertificate)
	}

	// Subject details
	X509_NAME *subject = ::X509_get_subject_name(cert);
	if(subject == 0)
	{
		::X509_free(cert);
		THROW_EXCEPTION(ConnectionException, TLSPeerCertificateInvalid)
	}
	
	// Common name
	char commonName[256];
	if(::X509_NAME_get_text_by_NID(subject, NID_commonName, commonName, sizeof(commonName)) <= 0)
	{
		::X509_free(cert);
		THROW_EXCEPTION(ConnectionException, TLSPeerCertificateInvalid)
	}
	// Terminate just in case
	commonName[sizeof(commonName)-1] = '\0';
	
	// Done.
	return std::string(commonName);
}
