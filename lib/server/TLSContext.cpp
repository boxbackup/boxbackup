// --------------------------------------------------------------------------
//
// File
//		Name:    TLSContext.h
//		Purpose: TLS (SSL) context for connections
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP
#include <openssl/ssl.h>

#include "autogen_ConnectionException.h"
#include "autogen_ServerException.h"
#include "CryptoUtils.h"
#include "SSLLib.h"
#include "TLSContext.h"

#include "MemLeakFindOn.h"

#define MAX_VERIFICATION_DEPTH		2
#define CIPHER_LIST			"ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"

// Macros to allow compatibility with OpenSSL 1.0 and 1.1 APIs. See
// https://github.com/charybdis-ircd/charybdis/blob/release/3.5/libratbox/src/openssl_ratbox.h
// for the gory details.
#if defined(LIBRESSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER >= 0x10100000L) // OpenSSL >= 1.1
#	define BOX_TLS_SERVER_METHOD TLS_server_method
#	define BOX_TLS_CLIENT_METHOD TLS_client_method
#else // OpenSSL < 1.1
#	define BOX_TLS_SERVER_METHOD TLSv1_server_method
#	define BOX_TLS_CLIENT_METHOD TLSv1_client_method
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::TLSContext()
//		Purpose: Constructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
TLSContext::TLSContext()
	: mpContext(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::~TLSContext()
//		Purpose: Destructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
TLSContext::~TLSContext()
{
	if(mpContext != 0)
	{
		::SSL_CTX_free(mpContext);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::Initialise(bool, const char *, const char *, const char *)
//		Purpose: Initialise the context, loading in the specified certificate and private key files
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void TLSContext::Initialise(bool AsServer, const char *CertificatesFile, const char *PrivateKeyFile, const char *TrustedCAsFile)
{
	if(mpContext != 0)
	{
		::SSL_CTX_free(mpContext);
	}

	mpContext = ::SSL_CTX_new(AsServer ? BOX_TLS_SERVER_METHOD() : BOX_TLS_CLIENT_METHOD());
	if(mpContext == NULL)
	{
		THROW_EXCEPTION(ServerException, TLSAllocationFailed)
	}
	
	// Setup our identity
	if(::SSL_CTX_use_certificate_chain_file(mpContext, CertificatesFile) != 1)
	{
		{
			THROW_EXCEPTION_MESSAGE(ServerException, TLSLoadCertificatesFailed,
				"Failed to load certificates from " << CertificatesFile << ": " <<
				CryptoUtils::LogError("loading certificates"));
		}
	}

	if(::SSL_CTX_use_PrivateKey_file(mpContext, PrivateKeyFile, SSL_FILETYPE_PEM) != 1)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, TLSLoadPrivateKeyFailed,
			"Failed to load private key from " << PrivateKeyFile << ": " <<
				CryptoUtils::LogError("loading private key"));
	}
	
	// Setup the identify of CAs we trust
	if(::SSL_CTX_load_verify_locations(mpContext, TrustedCAsFile, NULL) != 1)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, TLSLoadTrustedCAsFailed,
			"Failed to load CA certificate from " << TrustedCAsFile << ": " <<
				CryptoUtils::LogError("loading CA cert"));
	}
	
	// Setup options to require these certificates
	::SSL_CTX_set_verify(mpContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	// and a sensible maximum depth
	::SSL_CTX_set_verify_depth(mpContext, MAX_VERIFICATION_DEPTH);
	
	// Setup allowed ciphers
	if(::SSL_CTX_set_cipher_list(mpContext, CIPHER_LIST) != 1)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, TLSSetCiphersFailed,
			"Failed to set cipher list to " << CIPHER_LIST << ": " <<
				CryptoUtils::LogError("setting cipher list"));
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::GetRawContext()
//		Purpose: Get the raw context for OpenSSL API
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
SSL_CTX *TLSContext::GetRawContext() const
{
	if(mpContext == 0)
	{
		THROW_EXCEPTION(ServerException, TLSContextNotInitialised)
	}
	return mpContext;
}



