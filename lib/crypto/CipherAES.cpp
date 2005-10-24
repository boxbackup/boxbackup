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

