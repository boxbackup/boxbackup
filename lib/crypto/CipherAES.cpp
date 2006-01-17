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
//		Name:    CipherAES.cpp
//		Purpose: AES cipher description
//		Created: 27/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

// Only available in new versions of openssl
#ifndef PLATFORM_OLD_OPENSSL

#include <openssl/evp.h>

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE

#include "CipherAES.h"
#include "CipherException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherAES::CipherAES(CipherDescription::CipherMode, const void *, unsigned int, const void *)
//		Purpose: Constructor -- note key material and IV are not copied. KeyLength in bytes.
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
CipherAES::CipherAES(CipherDescription::CipherMode Mode, const void *pKey, unsigned int KeyLength, const void *pInitialisationVector)
	: CipherDescription(),
	  mMode(Mode),
	  mpKey(pKey),
	  mKeyLength(KeyLength),
	  mpInitialisationVector(pInitialisationVector)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherAES::CipherAES(const CipherAES &)
//		Purpose: Copy constructor
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
CipherAES::CipherAES(const CipherAES &rToCopy)
	: CipherDescription(rToCopy),
	  mMode(rToCopy.mMode),
	  mpKey(rToCopy.mpKey),
	  mKeyLength(rToCopy.mKeyLength),
	  mpInitialisationVector(rToCopy.mpInitialisationVector)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ~CipherAES::CipherAES()
//		Purpose: Destructor
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
CipherAES::~CipherAES()
{
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherAES::operator=(const CipherAES &)
//		Purpose: Assignment operator
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
CipherAES &CipherAES::operator=(const CipherAES &rToCopy)
{
	CipherDescription::operator=(rToCopy);

	mMode = rToCopy.mMode;
	mpKey = rToCopy.mpKey;
	mKeyLength = rToCopy.mKeyLength;
	mpInitialisationVector = rToCopy.mpInitialisationVector;

	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherAES::GetCipher()
//		Purpose: Returns cipher object
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
const EVP_CIPHER *CipherAES::GetCipher() const
{
	switch(mMode)
	{
	case CipherDescription::Mode_ECB:
		switch(mKeyLength)
		{
			case (128/8): return EVP_aes_128_ecb(); break;
			case (192/8): return EVP_aes_192_ecb(); break;
			case (256/8): return EVP_aes_256_ecb(); break;
		default:
			THROW_EXCEPTION(CipherException, EVPBadKeyLength)
			break;
		}
		break;
	
	case CipherDescription::Mode_CBC:
		switch(mKeyLength)
		{
			case (128/8): return EVP_aes_128_cbc(); break;
			case (192/8): return EVP_aes_192_cbc(); break;
			case (256/8): return EVP_aes_256_cbc(); break;
		default:
			THROW_EXCEPTION(CipherException, EVPBadKeyLength)
			break;
		}
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
//		Name:    CipherAES::SetupParameters(EVP_CIPHER_CTX *)
//		Purpose: Set up various parameters for cipher
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
void CipherAES::SetupParameters(EVP_CIPHER_CTX *pCipherContext) const
{
	ASSERT(pCipherContext != 0);
	
	// Set key (key length is implied)
	if(EVP_CipherInit_ex(pCipherContext, NULL, NULL, (unsigned char*)mpKey, (unsigned char*)mpInitialisationVector, -1) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
}



#endif // n PLATFORM_OLD_OPENSSL

