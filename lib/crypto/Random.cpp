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
#ifdef HAVE_RANDOM_DEVICE
	if(::RAND_load_file(RANDOM_DEVICE, 1024) != 1024)
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



