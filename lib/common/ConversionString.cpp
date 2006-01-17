// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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

