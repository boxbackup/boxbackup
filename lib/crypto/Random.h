// --------------------------------------------------------------------------
//
// File
//		Name:    Random.h
//		Purpose: Random numbers
//		Created: 31/12/03
//
// --------------------------------------------------------------------------

#ifndef RANDOM__H
#define RANDOM__H

#include <string>

namespace Random
{
	void Initialise();
	void Generate(void *pOutput, int Length);
	std::string GenerateHex(int Length);
	uint32_t RandomInt(uint32_t MaxValue);
};


#endif // RANDOM__H

