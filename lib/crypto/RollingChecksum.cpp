// --------------------------------------------------------------------------
//
// File
//		Name:    RollingChecksum.cpp
//		Purpose: A simple rolling checksum over a block of data
//		Created: 6/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "RollingChecksum.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    RollingChecksum::RollingChecksum(const void *, unsigned int)
//		Purpose: Constructor -- does initial computation of the checksum.
//		Created: 6/12/03
//
// --------------------------------------------------------------------------
RollingChecksum::RollingChecksum(const void * const data, const unsigned int Length)
	: a(0),
	  b(0)
{
	uint8_t * const block = (uint8_t * const)data;
	for(unsigned int x = Length; x >= 1; --x)
	{
		a += (*block);
		b += x * (*block);

		++block;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RollingChecksum::RollForwardSeveral(uint8_t*, uint8_t*, unsigned int, unsigned int)
//		Purpose: Move the checksum forward a block, given a pointer to the first byte of the current block,
//				 and a pointer just after the last byte of the current block and the length of the block and of the skip.
//		Created: 7/14/05
//
// --------------------------------------------------------------------------
void RollingChecksum::RollForwardSeveral(const uint8_t * const StartOfThisBlock, const uint8_t * const LastOfNextBlock, const unsigned int Length, const unsigned int Skip)
{
	// IMPLEMENTATION NOTE: Everything is implicitly mod 2^16 -- uint16_t's will overflow nicely.
	unsigned int i;
	uint16_t sumBegin=0, j,k;

	for(i=0; i < Skip; i++)
	{
		j = StartOfThisBlock[i];
		k = LastOfNextBlock[i];
		sumBegin += j;
		a += (k - j);
		b += a;
	}

	b -= Length * sumBegin;
}
