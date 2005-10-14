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

