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

#ifdef HAVE_OLD_SSL
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

#ifdef HAVE_OLD_SSL
	CipherDescription *Clone() const;
	void SetIV(const void *pIV);
#endif

private:
	CipherDescription::CipherMode mMode;
#ifndef HAVE_OLD_SSL
	const void *mpKey;
	unsigned int mKeyLength;
	const void *mpInitialisationVector;
#else
	std::string mKey;
	uint8_t mInitialisationVector[8];
#endif
};


#endif // CIPHERBLOWFISH__H

