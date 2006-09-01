// --------------------------------------------------------------------------
//
// File
//		Name:    WAFUKPostcode.cpp
//		Purpose: UK postcode validation and handling
//		Created: 31/8/05
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "WAFUKPostcode.h"

/*
Valid formats:

Outcode	Incode	Example
AN		NAA		M1 1AA
ANN		NAA		M60 1NW
AAN		NAA		CR2 6XH
AANN	NAA		DN55 1PT
ANA		NAA		W1P 1HQ
AANA	NAA		EC1A 1B
(A=alpha, N=numeric)
*/
#define MIN_POSTCODE_LENGTH		5
#define MAX_POSTCODE_LENGTH		7
#define	INCODE_LENGTH			3

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUKPostcode::WAFUKPostcode()
//		Purpose: Constructor
//		Created: 31/8/05
//
// --------------------------------------------------------------------------
WAFUKPostcode::WAFUKPostcode()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUKPostcode::~WAFUKPostcode()
//		Purpose: Destructor
//		Created: 31/8/05
//
// --------------------------------------------------------------------------
WAFUKPostcode::~WAFUKPostcode()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUKPostcode::ParseAndValidate(const std::string &rInput)
//		Purpose: Parse a postcode from a user, returning true if it's valid.
//		Created: 31/8/05
//
// --------------------------------------------------------------------------
bool WAFUKPostcode::ParseAndValidate(const std::string &rInput)
{
	// Make sure everything is blank (so validation checks later work)
	mOutcode.erase();
	mIncode.erase();
	
	// Look at the input
	const char *in = rInput.c_str();
	
	// Buffers for the normalised postcode
	char postcode[MAX_POSTCODE_LENGTH + 1];
	bool postcodeCharIsNumber[MAX_POSTCODE_LENGTH + 1];
	
	#define IS_IT_TOO_LONG	if(pcLen >= MAX_POSTCODE_LENGTH) return false;
	
	// Copy in the data, checking and transforming...
	int pcLen = 0;
	while(*in != '\0')
	{
		char i = *in;
		if(i == ' ' || i == '\t' || i == '\n')
		{
			// ignore this one
		}
		else if(i >= '0' && i <= '9')
		{
			// Number...
			IS_IT_TOO_LONG
			postcodeCharIsNumber[pcLen] = true;
			postcode[pcLen] = i;
			++pcLen;
		}
		else if(i >= 'A' && i <= 'Z')
		{
			// Letter, uppercase...
			IS_IT_TOO_LONG
			postcodeCharIsNumber[pcLen] = false;
			postcode[pcLen] = i;
			++pcLen;
		}
		else if(i >= 'a' && i <= 'z')
		{
			// Letter, lowercase...
			IS_IT_TOO_LONG
			postcodeCharIsNumber[pcLen] = false;
			postcode[pcLen] = i - ('a' - 'A');
			++pcLen;
		}
		else
		{
			return false;		// bad char
		}
	
		++in;
	}
	
	// quick check for it being too short
	if(pcLen < MIN_POSTCODE_LENGTH) return false;
	
	// Validate formats
	// First the incode, because it's easy
	if(postcodeCharIsNumber[pcLen - 3] != true
		|| postcodeCharIsNumber[pcLen - 2] != false
		|| postcodeCharIsNumber[pcLen - 1] != false)
	{
		return false;
	}
	
	// Then the various possibilities
	if(postcodeCharIsNumber[0] != false)
	{
		return false;
	}
	if(pcLen == 5 && postcodeCharIsNumber[1] != true)
	{
		return false;
	}
	if(pcLen == 6)
	{
		if(postcodeCharIsNumber[1] == false && postcodeCharIsNumber[2] == false)
		{
			return false;
		}
	}
	if(pcLen == 7)
	{
		if(postcodeCharIsNumber[1] != false || postcodeCharIsNumber[2] != true)
		{
			return false;
		}
	}
	
	// So it looks good then, store
	mOutcode.assign(postcode, pcLen - INCODE_LENGTH);
	mIncode.assign(postcode + pcLen - INCODE_LENGTH, INCODE_LENGTH);
	
	// Success
	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUKPostcode::NormalisedPostcode()
//		Purpose: Return a normalised postcode, or the empty string if it isn't valid
//		Created: 31/8/05
//
// --------------------------------------------------------------------------
std::string WAFUKPostcode::NormalisedPostcode() const
{
	if(!IsValid()) return std::string();
	
	return mOutcode + " " + mIncode;
}


