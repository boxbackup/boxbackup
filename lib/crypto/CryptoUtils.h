// --------------------------------------------------------------------------
//
// File
//		Name:    CryptoUtils.h
//		Purpose: Utility functions for dealing with the OpenSSL library
//		Created: 2012/04/26
//
// --------------------------------------------------------------------------

#ifndef CRYPTOUTILS__H
#define CRYPTOUTILS__H

#include <string>

// --------------------------------------------------------------------------
//
// Namespace
//		Name:    CryptoUtils
//		Purpose: Utility functions for dealing with the OpenSSL library
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
namespace CryptoUtils
{
	std::string LogError(const std::string& rErrorDuringAction);
};

#endif // CRYPTOUTILS__H

