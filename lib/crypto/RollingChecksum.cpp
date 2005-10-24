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
RollingChecksum::RollingChecksum(const void *data, unsigned int Length)
	: a(0),
	  b(0)
{
	uint8_t *block = (uint8_t *)data;
	for(unsigned int x = Length; x >= 1; --x)
	{
		a += (*block);
		b += x * (*block);
		
		++block;
	}
}



