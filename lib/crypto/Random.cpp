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
//		Name:    Random.cpp
//		Purpose: Random numbers
//		Created: 31/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <openssl/rand.h>
#include <stdio.h>

#include "Random.h"
#include "CipherException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    Random::Initialise()
//		Purpose: Add additional randomness to the standard library initialisation
//		Created: 18/6/04
//
// --------------------------------------------------------------------------
void Random::Initialise()
{
#ifndef PLATFORM_RANDOM_DEVICE_NONE
	if(::RAND_load_file(PLATFORM_RANDOM_DEVICE, 1024) != 1024)
	{
		THROW_EXCEPTION(CipherException, RandomInitFailed)
	}
#else
	::fprintf(stderr, "No random device -- additional seeding of random number generator not performed.\n");
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Random::Generate(void *, int)
//		Purpose: Generate Length bytes of random data
//		Created: 31/12/03
//
// --------------------------------------------------------------------------
void Random::Generate(void *pOutput, int Length)
{
	if(RAND_pseudo_bytes((uint8_t*)pOutput, Length) == -1)
	{
		THROW_EXCEPTION(CipherException, PseudoRandNotAvailable)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Random::GenerateHex(int)
//		Purpose: Generate Length bytes of hex encoded data. Note that the
//				 maximum length requested is limited. (Returns a string
//				 2 x Length characters long.)
//		Created: 1/11/04
//
// --------------------------------------------------------------------------
std::string Random::GenerateHex(int Length)
{
	uint8_t r[256];
	if(Length > sizeof(r))
	{
		THROW_EXCEPTION(CipherException, LengthRequestedTooLongForRandomHex)
	}
	Random::Generate(r, Length);
	
	std::string o;
	static const char *h = "0123456789abcdef";
	for(int l = 0; l < Length; ++l)
	{
		o += h[r[l] >> 4];
		o += h[r[l] & 0xf];
	}
	
	return o;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Random::RandomInt(int)
//		Purpose: Return a random integer between 0 and MaxValue inclusive.
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
uint32_t Random::RandomInt(uint32_t MaxValue)
{
	uint32_t v = 0;

	// Generate a mask
	uint32_t mask = 0;
	while(mask < MaxValue)
	{
		mask = (mask << 1) | 1;
	}

	do
	{
		// Generate a random number
		uint32_t r = 0;
		Random::Generate(&r, sizeof(r));
		
		// Mask off relevant bits
		v = r & mask;
		
		// Check that it's in the right range.
	} while(v > MaxValue);
	
	// NOTE: don't do a mod, because this doesn't give a correct random distribution
	
	return v;
}



