// --------------------------------------------------------------------------
//
// File
//		Name:    WAFFixedPoint.cpp
//		Purpose: Implement fixed point helper functions
//		Created: 7/2/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#include "WAFUtilityFns.h"
#include "autogen_WebAppFrameworkException.h"
#include "WebAppForm.h"

#include "MemLeakFindOn.h"

// Avoid lots of mulitplications in loops
static int fp_multiplier[] = {1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000};

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUtility::ParseFixedPointDecimal(const std::string &, int32_t &, int)
//		Purpose: See header for notes on parameters. Returns true if the string
//				 parsed correctly.
//		Created: 7/2/05
//
// --------------------------------------------------------------------------
bool WAFUtility::ParseFixedPointDecimal(const std::string &rString, int32_t &rIntOut, int ScaleDigits)
{
	ASSERT(ScaleDigits >= 0);
	if(ScaleDigits > WAFUTILITY_FIXEDPOINT_MAX_SCALE_DIGITS)
	{
		THROW_EXCEPTION(WebAppFrameworkException, FixedPointScaleTooLarge)
	}

	// Get string as simple c pointer
	const char *string = rString.c_str();

	// Parse the initial number
	errno = 0;			// required on some platforms
	char *end = 0;
	int integerPart = ::strtol(string, &end, 10 /* base */);
	if(errno != 0)
	{
		// Bad conversion -- no value or over/underflow, all bad.
		return false;
	}
	ASSERT(end != 0);

	// Try and parse the fractional part
	int fractionalPart = 0;
	if(*end != '\0')
	{
		// Skip whitespace
		while(::isspace(*end)) {++end;}
	
		// Is there a fractional part to parse?
		if(*end == '.')
		{
			// Yes, attempt to parse it
			++end;	// over .
			
			// Skip whitespace
			while(::isspace(*end)) {++end;}

			// First, copy the digits to another buffer
			// This is so if the user entered 1.0849724893274897238947239847892374
			// the fractional part can be parsed without overflowing a normal integer
			char fracBuffer[WAFUTILITY_FIXEDPOINT_MAX_SCALE_DIGITS + 4];
			int fractionalDigits = 0;
			for(fractionalDigits = 0; fractionalDigits < ScaleDigits; ++fractionalDigits)
			{
				if(*end >= '0' && *end <= '9')
				{
					fracBuffer[fractionalDigits] = *(end++);
				}
				else
				{
					break;
				}
			}
			fracBuffer[fractionalDigits] = '\0';	// terminate
			
			// Check that the rest of the string is just digits followed by whitespace
			{
				const char *check = end;
				while(*check >= '0' && *check <= '9')
				{
					++check;
				}
				while(::isspace(*check))
				{
					++check;
				}
				if(*check != '\0')
				{
					// Dodgy character in there somewhere
					return false;
				}
			}
			
			errno = 0;	// required
			fractionalPart = ::strtol(fracBuffer, 0, 10 /* base */);
			if(errno != 0)
			{
				// A bad conversion
				return false;
			}

			// Need to round up?
			if(fractionalDigits == ScaleDigits && (*end >= '0' && *end <= '9'))
			{
				// Yes, there's more digits than specified
				if(*end >= '5')
				{
					// And it means that the fractional part needs to be rounded up
					++fractionalPart;
				}
			}
			
			// Need to multipy up the result
			if(fractionalDigits < ScaleDigits)
			{
				// Not all the digits were supplied, so it needs to be multipled up
				for(int l = fractionalDigits; l < ScaleDigits; ++l)
				{
					fractionalPart *= 10;
				}
			}
		}
		else if(*end != '\0')
		{
			// Failed to parse
			return false;
		}
	}

	// Generate the fixed point integer
	int multipler = fp_multiplier[ScaleDigits];
	if(integerPart >= 0)
	{
		rIntOut = (integerPart * multipler) + fractionalPart;
	}
	else
	{
		rIntOut = (integerPart * multipler) - fractionalPart;
	}

	// Check for overflow
	if((rIntOut / multipler) != integerPart)
	{
		THROW_EXCEPTION(WebAppFrameworkException, FixedPointOverflow)
	}

	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUtility::FixedPointDecimalToString(int32_t, std::string &, int, int)
//		Purpose: See header for notes on paramters.
//		Created: 7/2/05
//
// --------------------------------------------------------------------------
void WAFUtility::FixedPointDecimalToString(int32_t Int, std::string &rStringOut, int ScaleDigits, int PlacesRequired)
{
	ASSERT(ScaleDigits >= 0);
	if(ScaleDigits > WAFUTILITY_FIXEDPOINT_MAX_SCALE_DIGITS)
	{
		THROW_EXCEPTION(WebAppFrameworkException, FixedPointScaleTooLarge)
	}
	if(PlacesRequired == -1) PlacesRequired = ScaleDigits;
	ASSERT(PlacesRequired >= 0);
	ASSERT(PlacesRequired <= ScaleDigits);

	// Generate divisor
	int divisor = fp_multiplier[ScaleDigits];

	// Generate format string first
	char format[64];
	::sprintf(format, "%%d.%%0%dd", ScaleDigits);

	// Then generate the actual string
	char output[64];
	int fractional = Int % divisor;
	if(fractional < 0) fractional = 0 - fractional;
	int e = ::sprintf(output, format, Int / divisor, fractional);
	
	// How many zeros could be trimmed?
	int zerosToTrim = ScaleDigits - PlacesRequired;
	if(ScaleDigits == 0) {zerosToTrim = 1;}	// fix for the a special case of zero places
	--e;
	while(e >= 0 && output[e] == '0' && zerosToTrim > 0)
	{
		output[e--] = '\0';	// remove the zero
		--zerosToTrim;
	}
	// Remove a trailing .
	if(e >= 0 && output[e] == '.')
	{
		output[e] = '\0';
	}
	
	// Return generated string
	rStringOut = output;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUtility::IntepretNumberFieldFixedPoint(...)
//		Purpose: Intepret an entered value on the form, as a fixed point value.
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
int8_t WAFUtility::IntepretNumberFieldFixedPoint(const std::string &rInput, int32_t &rNumberOut, int32_t BlankValue, bool BlankOK,
	int32_t RangeBegin, int32_t RangeEnd, bool HaveRangeBegin, bool HaveRangeEnd, int ScaleDigits)
{
	// See if there is anything there
	if(rInput.size() == 0)
	{
		if(BlankOK)
		{
			rNumberOut = BlankValue;
			return WebAppForm::Valid;
		}
		else
		{
			return WebAppForm::NumberFieldErr_Blank;
		}
	}

	// Convert the number
	int32_t r = 0;
	try
	{
		if(!WAFUtility::ParseFixedPointDecimal(rInput, r, ScaleDigits))
		{
			// Bad format
			return WebAppForm::NumberFieldErr_Format;
		}
	}
	catch(WebAppFrameworkException &e)
	{
		if(e.GetSubType() == WebAppFrameworkException::FixedPointOverflow)
		{
			// Overflowed, return a range error
			return WebAppForm::NumberFieldErr_Range;
		}
		else
		{
			throw;
		}
	}

	// Store the number decoded
	rNumberOut = r;
	
	// Then range check...
	if(HaveRangeBegin && (r < RangeBegin))
	{
		return WebAppForm::NumberFieldErr_Range;
	}
	if(HaveRangeEnd && (r > RangeEnd))
	{
		return WebAppForm::NumberFieldErr_Range;
	}

	return WebAppForm::Valid;
}

