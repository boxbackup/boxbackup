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
//		Name:    CipherDescription.h
//		Purpose: Pure virtual base class for describing ciphers
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#ifndef CIPHERDESCRIPTION__H
#define CIPHERDESCRIPTION__H

#ifndef BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE
	#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_FALSE
	class EVP_CIPHER;
	class EVP_CIPHER_CTX;
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    CipherDescription
//		Purpose: Describes a cipher
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
class CipherDescription
{
public:
	CipherDescription();
	CipherDescription(const CipherDescription &rToCopy);
	virtual ~CipherDescription();
	CipherDescription &operator=(const CipherDescription &rToCopy);
	
	// Return OpenSSL cipher object
	virtual const EVP_CIPHER *GetCipher() const = 0;
	
	// Setup any other parameters
	virtual void SetupParameters(EVP_CIPHER_CTX *pCipherContext) const = 0;
	
	// Mode parameter for cipher -- used in derived classes
	typedef enum
	{
		Mode_ECB = 0,
		Mode_CBC = 1,
		Mode_CFB = 2,
		Mode_OFB = 3
	} CipherMode;

#ifdef PLATFORM_OLD_OPENSSL
	// For the old version of OpenSSL, we need to be able to store cipher descriptions.
	virtual CipherDescription *Clone() const = 0;
	// And to be able to store new IVs
	virtual void SetIV(const void *pIV) = 0;
#endif
};

#endif // CIPHERDESCRIPTION__H

