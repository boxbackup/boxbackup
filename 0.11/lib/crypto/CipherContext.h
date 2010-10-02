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

#ifdef HAVE_OLD_SSL
	void OldOpenSSLFinal(unsigned char *Buffer, int &rOutLengthOut);
#endif
	
private:
	EVP_CIPHER_CTX ctx;
	bool mInitialised;
	bool mWithinTransform;
	bool mPaddingOn;
	uint8_t mGeneratedIV[CIPHERCONTEXT_MAX_GENERATED_IV_LENGTH];
#ifdef HAVE_OLD_SSL
	CipherFunction mFunction;
	CipherDescription *mpDescription;
#endif
};


#endif // CIPHERCONTEXT__H

