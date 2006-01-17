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
//		Name:    CipherContext.h
//		Purpose: Context for symmetric encryption / descryption
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#ifndef CIPHERCONTEXT__H
#define CIPHERCONTEXT__H

#ifdef BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_FALSE
	always include CipherContext.h first in any .cpp file
#endif
#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE
#include <openssl/evp.h>
class CipherDescription;

#define CIPHERCONTEXT_MAX_GENERATED_IV_LENGTH		32

// --------------------------------------------------------------------------
//
// Class
//		Name:    CipherContext
//		Purpose: Context for symmetric encryption / descryption
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
class CipherContext
{
public:
	CipherContext();
	~CipherContext();
private:
	CipherContext(const CipherContext &);	// no copying
	CipherContext &operator=(const CipherContext &);	// no assignment
public:

	typedef enum
	{
		Decrypt = 0,
		Encrypt = 1
	} CipherFunction;

	void Init(CipherContext::CipherFunction Function, const CipherDescription &rDescription);
	void Reset();
	
	void Begin();
	int Transform(void *pOutBuffer, int OutLength, const void *pInBuffer, int InLength);
	int Final(void *pOutBuffer, int OutLength);
	int InSizeForOutBufferSize(int OutLength);
	int MaxOutSizeForInBufferSize(int InLength);
	
	int TransformBlock(void *pOutBuffer, int OutLength, const void *pInBuffer, int InLength);

	bool IsInitialised() {return mInitialised;}
	
	int GetIVLength();
	void SetIV(const void *pIV);
	const void *SetRandomIV(int &rLengthOut);
	
	void UsePadding(bool Padding = true);

#ifdef PLATFORM_OLD_OPENSSL
	void OldOpenSSLFinal(unsigned char *Buffer, int &rOutLengthOut);
#endif
	
private:
	EVP_CIPHER_CTX ctx;
	bool mInitialised;
	bool mWithinTransform;
	bool mPaddingOn;
	uint8_t mGeneratedIV[CIPHERCONTEXT_MAX_GENERATED_IV_LENGTH];
#ifdef PLATFORM_OLD_OPENSSL
	CipherFunction mFunction;
	CipherDescription *mpDescription;
#endif
};


#endif // CIPHERCONTEXT__H

