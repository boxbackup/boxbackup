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
#error	"always include CipherContext.h before any other crypto headers"
#endif

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE

#include <string>

#include <openssl/evp.h>

class CipherDescription;

#define CIPHERCONTEXT_MAX_GENERATED_IV_LENGTH		32

// Macros to allow compatibility with OpenSSL 1.0 and 1.1 APIs. See
// https://github.com/charybdis-ircd/charybdis/blob/release/3.5/libratbox/src/openssl_ratbox.h
// for the gory details.
#if defined(LIBRESSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER >= 0x10100000L) // OpenSSL >= 1.1
#	define BOX_OPENSSL_INIT_CTX(ctx) ctx = EVP_CIPHER_CTX_new();
#	define BOX_OPENSSL_CTX(ctx) ctx
#	define BOX_OPENSSL_CLEANUP_CTX(ctx) EVP_CIPHER_CTX_free(ctx)
typedef EVP_CIPHER_CTX* BOX_EVP_CIPHER_CTX;
#else // OpenSSL < 1.1
#	define BOX_OPENSSL_INIT_CTX(ctx) EVP_CIPHER_CTX_init(&ctx); // no error return code, even though the docs says it does
#	define BOX_OPENSSL_CTX(ctx) &ctx
#	define BOX_OPENSSL_CLEANUP_CTX(ctx) EVP_CIPHER_CTX_cleanup(&ctx)
typedef EVP_CIPHER_CTX BOX_EVP_CIPHER_CTX;
#endif


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
protected:
	std::string LogError(const std::string& operation);
public:

	typedef enum
	{
		None = 0,
		Decrypt,
		Encrypt
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
	const char* GetFunction() const
	{
		return (mFunction == Encrypt) ? "encrypt" : "decrypt";
	}

#ifdef HAVE_OLD_SSL
	void OldOpenSSLFinal(unsigned char *Buffer, int &rOutLengthOut);
#endif
	
private:
	BOX_EVP_CIPHER_CTX ctx;
	bool mInitialised;
	bool mWithinTransform;
	bool mPaddingOn;
	CipherFunction mFunction;
	std::string mCipherName;
	const CipherDescription *mpDescription;
	std::string mIV;
};


#endif // CIPHERCONTEXT__H

