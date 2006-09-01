// --------------------------------------------------------------------------
//
// File
//		Name:    ValidateEmailAddress.h
//		Purpose: Validate an email address.
//		Created: 7/1/05
//
// --------------------------------------------------------------------------

#ifndef VALIDATEEMAILADDRESS__H
#define VALIDATEEMAILADDRESS__H

#include <string>

bool ValidateEmailAddress(const std::string &rEmailAddress, std::string &rCanonicalAddressOut, bool CheckDomainWithDNS = true);

#endif // VALIDATEEMAILADDRESS__H

