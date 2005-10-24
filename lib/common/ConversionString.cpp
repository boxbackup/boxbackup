// --------------------------------------------------------------------------
//
// File
//		Name:    ConversionString.cpp
//		Purpose: Conversions to and from strings
//		Created: 9/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include "Conversion.h"
#include "autogen_ConversionException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BoxConvert::_ConvertStringToInt(const char *, int)
//		Purpose: Convert from string to integer, with range checking.
//				 Always does signed -- no point in unsigned as C++ type checking
//				 isn't up to handling it properly.
//				 If a null pointer is passed in, then returns 0.
//		Created: 9/4/04
//
// --------------------------------------------------------------------------
int32_t BoxConvert::_ConvertStringToInt(const char *pString, int Size)
{
	// Handle null strings gracefully.
	if(pString == 0)
	{
		return 0;
	}

	// Check for initial validity
	if(*pString == '\0')
	{
		THROW_EXCEPTION(ConversionException, CannotConvertEmptyStringToInt)
	}
	
	// Convert.
	char *numEnd = 0;
	long r = ::strtol(pString, &numEnd, 0);

	// Check that all the characters were used
	if(*numEnd != '\0')
	{
		THROW_EXCEPTION(ConversionException, BadStringRepresentationOfInt)
	}
	
	// Error check
	if(r == 0 && errno == EINVAL)
	{
		THROW_EXCEPTION(ConversionException, BadStringRepresentationOfInt)
	}

	// Range check from strtol
	if((r == LONG_MIN || r == LONG_MAX) && errno == ERANGE)
	{
		THROW_EXCEPTION(ConversionException, IntOverflowInConvertFromString)
	}
	
	// Check range for size of integer
	switch(Size)
	{
	case 32:
		{
			// No extra checking needed, if this assert holds true
			ASSERT(sizeof(long) == sizeof(int32_t));
		}
		break;
		
	case 16:
		{
			if(r <= (0 - 0x7fff) || r > 0x7fff)
			{
				THROW_EXCEPTION(ConversionException, IntOverflowInConvertFromString)
			}
			break;
		}
	
	case 8:
		{
			if(r <= (0 - 0x7f) || r > 0x7f)
			{
				THROW_EXCEPTION(ConversionException, IntOverflowInConvertFromString)
			}
			break;
		}
		
	default:
		{
			THROW_EXCEPTION(ConversionException, BadIntSize)
			break;
		}
	}
	
	// Return number
	return r;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BoxConvert::_ConvertIntToString(std::string &, int32_t)
//		Purpose: Convert signed interger to a string
//		Created: 9/4/04
//
// --------------------------------------------------------------------------
void BoxConvert::_ConvertIntToString(std::string &rTo, int32_t From)
{
	char text[64];	// size more than enough
	::sprintf(text, "%d", From);
	rTo = text;
}

