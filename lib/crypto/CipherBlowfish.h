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
//		Name:    CipherBlowfish.h
//		Purpose: Blowfish cipher description
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#ifndef CIPHERBLOWFISH__H
#define CIPHERBLOWFISH__H

#ifdef PLATFORM_OLD_OPENSSL
	#include <string>
#endif

#include "CipherDescription.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    CipherBlowfish
//		Purpose: Description of Blowfish cipher parameters -- note that copies are not made of key material and IV, careful with object lifetimes.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
class CipherBlowfish : public CipherDescription
{
public:
	CipherBlowfish(CipherDescription::CipherMode Mode, const void *pKey, unsigned int KeyLength, const void *pInitialisationVector = 0);
	CipherBlowfish(const CipherBlowfish &rToCopy);
	virtual ~CipherBlowfish();
	CipherBlowfish &operator=(const CipherBlowfish &rToCopy);
	
	// Return OpenSSL cipher object
	virtual const EVP_CIPHER *GetCipher() const;
	
	// Setup any other parameters
	virtual void SetupParameters(EVP_CIPHER_CTX *pCipherContext) const;

#ifdef PLATFORM_OLD_OPENSSL
	CipherDescription *Clone() const;
	void SetIV(const void *pIV);
#endif

private:
	CipherDescription::CipherMode mMode;
#ifndef PLATFORM_OLD_OPENSSL
	const void *mpKey;
	unsigned int mKeyLength;
	const void *mpInitialisationVector;
#else
	std::string mKey;
	uint8_t mInitialisationVector[8];
#endif
};


#endif // CIPHERBLOWFISH__H

