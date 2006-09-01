// --------------------------------------------------------------------------
//
// File
//		Name:    WAFPhoneNumber.cpp
//		Purpose: Implement functions for phone number fields
//		Created: 11/2/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <ctype.h>

#include "WAFUtilityFns.h"

#include "MemLeakFindOn.h"

#define MIN_DIGITS_IN_PHONE_NUMBER	7


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUtility::ValidatePhoneNumber(const std::string &, std::string)
//		Purpose: Return true if the field is a valid phone number. Also, turn the
//				 phone number into a canonocial form.
//		Created: 11/2/05
//
// --------------------------------------------------------------------------
bool WAFUtility::ValidatePhoneNumber(const std::string &rInput, std::string &rCanonicalPhoneNumberOut)
{
	// For output
	std::string canonical;
	
	// Get string
	const char *phone = rInput.c_str();
	
	// Skip leading whitespace
	while(::isspace(*phone))
	{
		++phone;
	}

	// A plus is only allowed at the beginning, so need special validation
	if(*phone == '+')
	{
		canonical += '+';
		++phone;
	}
	
	// Loop though scanning characters
	bool whitespace = false;
	int numDigits = 0;
	while(*phone != '\0')
	{
		if(::isspace(*phone))
		{
			// Flag for whitespace later (to contract multiple spaces into one)
			whitespace = true;
		}
		else if((*phone >= '0' && *phone <= '9') || *phone == '.' || *phone == ',')
		{
			// Count digits
			if(*phone >= '0' && *phone <= '9')
			{
				++numDigits;
			}
			
			// Any whitespace to add?
			if(whitespace)
			{
				canonical += ' ';
				whitespace = false;
			}
			
			// Add character
			canonical += *phone;
		}
		else
		{
			// Bad phone number
			return false;
		}
		
		++phone;
	}
	
	// Are there enough digits?
	if(numDigits < MIN_DIGITS_IN_PHONE_NUMBER)
	{
		return false;
	}

	// Return where phone number is valid
	rCanonicalPhoneNumberOut = canonical;
	return true;
}


