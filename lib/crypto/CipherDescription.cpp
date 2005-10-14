// --------------------------------------------------------------------------
//
// File
//		Name:    CipherDescription.cpp
//		Purpose: Pure virtual base class for describing ciphers
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <openssl/evp.h>

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE

#include "CipherDescription.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherDescription::CipherDescription()
//		Purpose: Constructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherDescription::CipherDescription()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherDescription::CipherDescription(const CipherDescription &)
//		Purpose: Copy constructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherDescription::CipherDescription(const CipherDescription &rToCopy)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ~CipherDescription::CipherDescription()
//		Purpose: Destructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherDescription::~CipherDescription()
{
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherDescription::operator=(const CipherDescription &)
//		Purpose: Assignment operator
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherDescription &CipherDescription::operator=(const CipherDescription &rToCopy)
{
	return *this;
}


