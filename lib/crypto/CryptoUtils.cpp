// --------------------------------------------------------------------------
//
// File
//		Name:    CryptoUtils.cpp
//		Purpose: Utility functions for dealing with the OpenSSL library
//		Created: 2012/04/26
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "CryptoUtils.h"
#include "Exception.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CryptoUtils::LogError(const char *)
//		Purpose: Logs an error from the OpenSSL library
//		Created: 2012/04/26
//
// --------------------------------------------------------------------------
std::string CryptoUtils::LogError(const std::string& rErrorDuringAction)
{
	unsigned long errcode;
	char errname[256];		// SSL docs say at least 120 bytes
	std::string firstError;

	while((errcode = ERR_get_error()) != 0)
	{
		::ERR_error_string_n(errcode, errname, sizeof(errname));
		if(firstError.empty())
		{
			firstError = errname;
		}
		BOX_ERROR("SSL or crypto error: " << rErrorDuringAction <<
			": " << errname);
	}
	return firstError;
}

