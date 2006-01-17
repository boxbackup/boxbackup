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
//		Name:    TLSContext.h
//		Purpose: TLS (SSL) context for connections
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP
#include <openssl/ssl.h>

#include "TLSContext.h"
#include "ServerException.h"
#include "SSLLib.h"
#include "TLSContext.h"

#include "MemLeakFindOn.h"

#define MAX_VERIFICATION_DEPTH		2
#define CIPHER_LIST					"ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"

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
	mpContext = ::SSL_CTX_new(AsServer?TLSv1_server_method():TLSv1_client_method());
	if(mpContext == NULL)
	{
		THROW_EXCEPTION(ServerException, TLSAllocationFailed)
	}
	
	// Setup our identity
	if(::SSL_CTX_use_certificate_chain_file(mpContext, CertificatesFile) != 1)
	{
		SSLLib::LogError("Load certificates");
		THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
	}
	if(::SSL_CTX_use_PrivateKey_file(mpContext, PrivateKeyFile, SSL_FILETYPE_PEM) != 1)
	{
		SSLLib::LogError("Load private key");
		THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
	}
	
	// Setup the identify of CAs we trust
	if(::SSL_CTX_load_verify_locations(mpContext, TrustedCAsFile, NULL) != 1)
	{
		SSLLib::LogError("Load CA cert");
		THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
	}
	
	// Setup options to require these certificates
	::SSL_CTX_set_verify(mpContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	// and a sensible maximum depth
	::SSL_CTX_set_verify_depth(mpContext, MAX_VERIFICATION_DEPTH);
	
	// Setup allowed ciphers
	if(::SSL_CTX_set_cipher_list(mpContext, CIPHER_LIST) != 1)
	{
		SSLLib::LogError("Set cipher list");
		THROW_EXCEPTION(ServerException, TLSSetCiphersFailed)
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



