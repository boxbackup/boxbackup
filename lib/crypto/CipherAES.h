// --------------------------------------------------------------------------
//
// File
//		Name:    CipherAES.h
//		Purpose: AES cipher description
//		Created: 27/4/04
//
// --------------------------------------------------------------------------

#ifndef CIPHERAES__H
#define CIPHERAES__H

// Only available in new versions of openssl
#ifndef HAVE_OLD_SSL

#include "CipherDescription.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    CipherAES
//		Purpose: AES cipher description
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
class CipherAES : public CipherDescription
{
public:
	CipherAES(CipherDescription::CipherMode Mode, const void *pKey, unsigned int KeyLength, const void *pInitialisationVector = 0);
	CipherAES(const CipherAES &rToCopy);
	virtual ~CipherAES();
	CipherAES &operator=(const CipherAES &rToCopy);
	
	// Return OpenSSL cipher object
	virtual const EVP_CIPHER *GetCipher() const;
	
	// Setup any other parameters
	virtual void SetupParameters(EVP_CIPHER_CTX *pCipherContext) const;

private:
	CipherDescription::CipherMode mMode;
	const void *mpKey;
	unsigned int mKeyLength;
	const void *mpInitialisationVector;
};

#endif // n HAVE_OLD_SSL

#endif // CIPHERAES__H

