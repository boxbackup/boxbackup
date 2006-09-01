// --------------------------------------------------------------------------
//
// File
//		Name:    ValidateEmailAddress.cpp
//		Purpose: 
//		Created: 7/1/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef PLATFORM_DARWIN
	// Required to T_MX etc on Darwin
	#define BIND_8_COMPAT
#endif

#include <ctype.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>

#include "ValidateEmailAddress.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ValidateEmailAddress(const std::string &, std::string &, bool)
//		Purpose: Returns true if the email address is likely to be valid.
//				 Optionally attempts to look the domain name up in DNS.
//		Created: 7/1/05
//
// --------------------------------------------------------------------------
bool ValidateEmailAddress(const std::string &rEmailAddress, std::string &rCanonicalAddressOut, bool CheckDomainWithDNS)
{
	bool valid = true;
	
	// Scan the string, copying into the canonical address out one
	const char *e = rEmailAddress.c_str();
	bool inDomain = false;
	int dotsInDomain = 0;
	bool foundNonDotInDomain = false;
	bool startingDomainElement = false;
	unsigned int domainStarts = 0;
	std::string canonical;
	while(*e != '\0')
	{
		char c = *e;
		// Only look at non-whitespace characters
		if(!::isspace(c))
		{
			if(c == '@')
			{
				if(inDomain)
				{
					// Only have one @ sign
					valid = false;
				}
				else
				{
					inDomain = true;
					startingDomainElement = true;
					// Any left hand side?
					if(canonical.size() == 0)
					{
						valid = false;
					}
					// Record where the domain started
					domainStarts = canonical.size() + 1;
				}
			}
			// Domain specific checks
			else if(inDomain)
			{
				// Domains must go to lower case
				c = ::tolower(c);

				// Check for dots
				if(c == '.')
				{
					++dotsInDomain;
					
					// Check that the last char wasn't a . as well
					if(canonical.size() >= 1 && canonical[canonical.size() - 1] == '.')
					{
						valid = false;
					}
				}
				else
				{
					foundNonDotInDomain = true;
				}
				
				// Check valid characters
				if(!
					(
						(c == '.')
						|| (c >= 'a' && c <= 'z')
						|| (c >= '0' && c <= '9')
					)
				)
				{
					if(c == '-')
					{
						// - is not allowed as first element of domain
						if(startingDomainElement)
						{
							valid = false;
						}
					}
					else
					{
						valid = false;
					}
				}

				// Unflag as start of domain element
				startingDomainElement = false;

				if(c == '.')
				{
					// Mark as starting a domain element
					startingDomainElement = true;
				}
			}
			
			
			// Append char to string.
			canonical += c;
		}

		// Next char
		++e;
	}

	// Remove trailing dot from canonical address?
	if(inDomain)
	{
		if(canonical[canonical.size() - 1] == '.')
		{
			// has a final dot. Remove it from the address
			canonical.resize(canonical.size() - 1);
			// One less dot found, though
			--dotsInDomain;
		}
	}

	// Some more checks
	if(!inDomain || (dotsInDomain == 0) || !foundNonDotInDomain)
	{
		valid = false;
	}

	// If the domain looks valid, check it in DNS
	if(valid && CheckDomainWithDNS)
	{
		unsigned char answer[1024];
		const char *domain = canonical.c_str() + domainStarts;
		if(::res_query(domain, C_IN, T_MX, answer, sizeof(answer)) < 0
			&& h_errno == HOST_NOT_FOUND)
		{
			// MX record not found, try the A record instead
			if(::res_query(domain, C_IN, T_A, answer, sizeof(answer)) < 0
				&& h_errno == HOST_NOT_FOUND)
			{
				valid = false;
			}
		}
		else
		{
			// got a reply or there was a lookup failure, so assume the domain is valid
		}
	}

	// Copy the canonical address out, and return the validity flag
	rCanonicalAddressOut = canonical;
	return valid;
}



