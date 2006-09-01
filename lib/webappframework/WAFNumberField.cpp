// --------------------------------------------------------------------------
//
// File
//		Name:    WAFNumberField.cpp
//		Purpose: Implement functions for number fields
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
#include "WebAppForm.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFUtility::IntepretNumberField(...)
//		Purpose: Intepret an entered value on the form.
//		Created: 17/4/04
//
// --------------------------------------------------------------------------
int8_t WAFUtility::IntepretNumberField(const std::string &rInput, int32_t &rNumberOut, int32_t BlankValue, bool BlankOK,
	int32_t RangeBegin, int32_t RangeEnd, bool HaveRangeBegin, bool HaveRangeEnd)
{
	bool nonNumCharsFound = false;
	
	// find all the number characters
	std::string n;
	for(std::string::const_iterator i(rInput.begin()); i != rInput.end(); ++i)
	{
		if(*i >= '0' && *i <= '9')
		{
			// OK character, add to string
			n += *i;
		}
		else
		{
			if((*i == '-') && (n.size() == 0))
			{
				// - is OK, but only as the first character
				n += '-';
			}
			else
			{
				// Bad character found
				nonNumCharsFound = true;
			}
		}
	}
	
	// Right then... see what we've got
	if(n.size() == 0)
	{
		// Was it really blank?
		if(nonNumCharsFound)
		{
			return WebAppForm::NumberFieldErr_FormatBlank;
		}
		else
		{
			// Yes, was blank, is this an error or not?
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
	}
	
	// Convert the number
	errno = 0;	// necessary on some platforms
	int32_t r = ::strtol(n.c_str(), 0, 10);

	// Error check
	if(r == 0 && errno == EINVAL)
	{
		// Shouldn't get this as the number only contains valid characters
		return WebAppForm::NumberFieldErr_Format;
	}

	// Range check from strtol
	if((r == LONG_MIN || r == LONG_MAX) && errno == ERANGE)
	{
		return WebAppForm::NumberFieldErr_Range;
	}

	// Store the number decoded
	rNumberOut = r;
	
	// Check format
	if(nonNumCharsFound)
	{
		return WebAppForm::NumberFieldErr_Format;
	}

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

