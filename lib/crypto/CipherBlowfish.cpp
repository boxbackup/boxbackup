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
//		Name:    CipherBlowfish.cpp
//		Purpose: Blowfish cipher description
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <openssl/evp.h>

#ifdef PLATFORM_OLD_OPENSSL
	#include <string.h>
	#include <strings.h>
#endif

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE

#include "CipherBlowfish.h"
#include "CipherException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::CipherBlowfish(CipherDescription::CipherMode, const void *, unsigned int, const void *)
//		Purpose: Constructor -- note key material and IV are not copied. KeyLength in bytes.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::CipherBlowfish(CipherDescription::CipherMode Mode, const void *pKey, unsigned int KeyLength, const void *pInitialisationVector)
	: CipherDescription(),
	  mMode(Mode)
#ifndef PLATFORM_OLD_OPENSSL
	, mpKey(pKey),
	  mKeyLength(KeyLength),
	  mpInitialisationVector(pInitialisationVector)
{
}
#else
{
	mKey.assign((const char *)pKey, KeyLength);
	if(pInitialisationVector == 0)
	{
		bzero(mInitialisationVector, sizeof(mInitialisationVector));
	}
	else
	{
		::memcpy(mInitialisationVector, pInitialisationVector, sizeof(mInitialisationVector));
	}
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::CipherBlowfish(const CipherBlowfish &)
//		Purpose: Copy constructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::CipherBlowfish(const CipherBlowfish &rToCopy)
	: CipherDescription(rToCopy),
	  mMode(rToCopy.mMode),
#ifndef PLATFORM_OLD_OPENSSL
	  mpKey(rToCopy.mpKey),
	  mKeyLength(rToCopy.mKeyLength),
	  mpInitialisationVector(rToCopy.mpInitialisationVector)
{
}
#else
	  mKey(rToCopy.mKey)
{
	::memcpy(mInitialisationVector, rToCopy.mInitialisationVector, sizeof(mInitialisationVector));
}
#endif


#ifdef PLATFORM_OLD_OPENSSL
// Hack functions to support old OpenSSL API
CipherDescription *CipherBlowfish::Clone() const
{
	return new CipherBlowfish(*this);
}
void CipherBlowfish::SetIV(const void *pIV)
{
	if(pIV == 0)
	{
		bzero(mInitialisationVector, sizeof(mInitialisationVector));
	}
	else
	{
		::memcpy(mInitialisationVector, pIV, sizeof(mInitialisationVector));
	}
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    ~CipherBlowfish::CipherBlowfish()
//		Purpose: Destructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::~CipherBlowfish()
{
#ifdef PLATFORM_OLD_OPENSSL
	// Zero copy of key
	for(unsigned int l = 0; l < mKey.size(); ++l)
	{
		mKey[l] = '\0';
	}
#endif
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::operator=(const CipherBlowfish &)
//		Purpose: Assignment operator
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish &CipherBlowfish::operator=(const CipherBlowfish &rToCopy)
{
	CipherDescription::operator=(rToCopy);

	mMode = rToCopy.mMode;
#ifndef PLATFORM_OLD_OPENSSL
	mpKey = rToCopy.mpKey;
	mKeyLength = rToCopy.mKeyLength;
	mpInitialisationVector = rToCopy.mpInitialisationVector;
#else
	mKey = rToCopy.mKey;
	::memcpy(mInitialisationVector, rToCopy.mInitialisationVector, sizeof(mInitialisationVector));
#endif

	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::GetCipher()
//		Purpose: Returns cipher object
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
const EVP_CIPHER *CipherBlowfish::GetCipher() const
{
	switch(mMode)
	{
	case CipherDescription::Mode_ECB:
		return EVP_bf_ecb();
		break;
	
	case CipherDescription::Mode_CBC:
		return EVP_bf_cbc();
		break;
	
	case CipherDescription::Mode_CFB:
		return EVP_bf_cfb();
		break;
	
	case CipherDescription::Mode_OFB:
		return EVP_bf_ofb();
		break;
	
	default:
		break;
	}

	// Unknown!
	THROW_EXCEPTION(CipherException, UnknownCipherMode)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::SetupParameters(EVP_CIPHER_CTX *)
//		Purpose: Set up various parameters for cipher
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void CipherBlowfish::SetupParameters(EVP_CIPHER_CTX *pCipherContext) const
{
	ASSERT(pCipherContext != 0);
	
	// Set key length
#ifndef PLATFORM_OLD_OPENSSL
	if(EVP_CIPHER_CTX_set_key_length(pCipherContext, mKeyLength) != 1)
#else
	if(EVP_CIPHER_CTX_set_key_length(pCipherContext, mKey.size()) != 1)
#endif
	{
		THROW_EXCEPTION(CipherException, EVPBadKeyLength)
	}
	// Set key
#ifndef PLATFORM_OLD_OPENSSL
	if(EVP_CipherInit_ex(pCipherContext, NULL, NULL, (unsigned char*)mpKey, (unsigned char*)mpInitialisationVector, -1) != 1)
#else
	if(EVP_CipherInit(pCipherContext, NULL, (unsigned char*)mKey.c_str(), (unsigned char*)mInitialisationVector, -1) != 1)
#endif
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
}



