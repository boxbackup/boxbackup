// --------------------------------------------------------------------------
//
// File
//		Name:    CipherBlowfish.cpp
//		Purpose: Blowfish cipher description
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <openssl/evp.h>

#ifdef HAVE_OLD_SSL
	#include <string.h>
	#include <strings.h>
#endif

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE

#include "CipherBlowfish.h"
#include "CipherException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::CipherBlowfish(CipherDescription::CipherMode, const void *, unsigned int, const void *)
//		Purpose: Constructor -- note key material and IV are not copied. KeyLength in bytes.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::CipherBlowfish(CipherDescription::CipherMode Mode, const void *pKey, unsigned int KeyLength, const void *pInitialisationVector)
	: CipherDescription(),
	  mMode(Mode)
#ifndef HAVE_OLD_SSL
	, mpKey(pKey),
	  mKeyLength(KeyLength),
	  mpInitialisationVector(pInitialisationVector)
{
}
#else
{
	mKey.assign((const char *)pKey, KeyLength);
	if(pInitialisationVector == 0)
	{
		bzero(mInitialisationVector, sizeof(mInitialisationVector));
	}
	else
	{
		::memcpy(mInitialisationVector, pInitialisationVector, sizeof(mInitialisationVector));
	}
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::CipherBlowfish(const CipherBlowfish &)
//		Purpose: Copy constructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::CipherBlowfish(const CipherBlowfish &rToCopy)
	: CipherDescription(rToCopy),
	  mMode(rToCopy.mMode),
#ifndef HAVE_OLD_SSL
	  mpKey(rToCopy.mpKey),
	  mKeyLength(rToCopy.mKeyLength),
	  mpInitialisationVector(rToCopy.mpInitialisationVector)
{
}
#else
	  mKey(rToCopy.mKey)
{
	::memcpy(mInitialisationVector, rToCopy.mInitialisationVector, sizeof(mInitialisationVector));
}
#endif


#ifdef HAVE_OLD_SSL
// Hack functions to support old OpenSSL API
CipherDescription *CipherBlowfish::Clone() const
{
	return new CipherBlowfish(*this);
}
void CipherBlowfish::SetIV(const void *pIV)
{
	if(pIV == 0)
	{
		bzero(mInitialisationVector, sizeof(mInitialisationVector));
	}
	else
	{
		::memcpy(mInitialisationVector, pIV, sizeof(mInitialisationVector));
	}
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    ~CipherBlowfish::CipherBlowfish()
//		Purpose: Destructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish::~CipherBlowfish()
{
#ifdef HAVE_OLD_SSL
	// Zero copy of key
	for(unsigned int l = 0; l < mKey.size(); ++l)
	{
		mKey[l] = '\0';
	}
#endif
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::operator=(const CipherBlowfish &)
//		Purpose: Assignment operator
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherBlowfish &CipherBlowfish::operator=(const CipherBlowfish &rToCopy)
{
	CipherDescription::operator=(rToCopy);

	mMode = rToCopy.mMode;
#ifndef HAVE_OLD_SSL
	mpKey = rToCopy.mpKey;
	mKeyLength = rToCopy.mKeyLength;
	mpInitialisationVector = rToCopy.mpInitialisationVector;
#else
	mKey = rToCopy.mKey;
	::memcpy(mInitialisationVector, rToCopy.mInitialisationVector, sizeof(mInitialisationVector));
#endif

	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherBlowfish::GetCipher()
//		Purpose: Returns cipher object
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
const EVP_CIPHER *CipherBlowfish::GetCipher() const
{
	switch(mMode)
	{
	case CipherDescription::Mode_ECB:
		return EVP_bf_ecb();
		break;
	
	case CipherDescription::Mode_CBC:
		return EVP_bf_cbc();
		break;
	
	case CipherDescription::Mode_CFB:
		return EVP_bf_cfb();
		break;
	
	case CipherDescription::Mode_OFB:
		return EVP_bf_ofb();
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
//		Name:    CipherBlowfish::SetupParameters(EVP_CIPHER_CTX *)
//		Purpose: Set up various parameters for cipher
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void CipherBlowfish::SetupParameters(EVP_CIPHER_CTX *pCipherContext) const
{
	ASSERT(pCipherContext != 0);
	
	// Set key length
#ifndef HAVE_OLD_SSL
	if(EVP_CIPHER_CTX_set_key_length(pCipherContext, mKeyLength) != 1)
#else
	if(EVP_CIPHER_CTX_set_key_length(pCipherContext, mKey.size()) != 1)
#endif
	{
		THROW_EXCEPTION(CipherException, EVPBadKeyLength)
	}
	// Set key
#ifndef HAVE_OLD_SSL
	if(EVP_CipherInit_ex(pCipherContext, NULL, NULL, (unsigned char*)mpKey, (unsigned char*)mpInitialisationVector, -1) != 1)
#else
	if(EVP_CipherInit(pCipherContext, NULL, (unsigned char*)mKey.c_str(), (unsigned char*)mInitialisationVector, -1) != 1)
#endif
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
}



