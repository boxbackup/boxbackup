// --------------------------------------------------------------------------
//
// File
//		Name:    RollingChecksum.h
//		Purpose: A simple rolling checksum over a block of data
//		Created: 6/12/03
//
// --------------------------------------------------------------------------

#ifndef ROLLINGCHECKSUM__H
#define ROLLINGCHECKSUM__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    RollingChecksum
//		Purpose: A simple rolling checksum over a block of data -- can move the block
//				 "forwards" in memory and get the next checksum efficiently.
//
//				 Implementation of http://rsync.samba.org/tech_report/node3.html
//		Created: 6/12/03
//
// --------------------------------------------------------------------------
class RollingChecksum
{
public:
	RollingChecksum(const void * const data, const unsigned int Length);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    RollingChecksum::RollForward(uint8_t, uint8_t, unsigned int)
	//		Purpose: Move the checksum forward a block, given the first byte of the current block,
	//				 last byte of the next block (it's rolling forward to) and the length of the block.
	//		Created: 6/12/03
	//
	// --------------------------------------------------------------------------
	inline void RollForward(const uint8_t StartOfThisBlock, const uint8_t LastOfNextBlock, const unsigned int Length)
	{
		// IMPLEMENTATION NOTE: Everything is implicitly mod 2^16 -- uint16_t's will overflow nicely.
		a -= StartOfThisBlock;
		a += LastOfNextBlock;
		b -= Length * StartOfThisBlock;
		b += a;
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
	void RollForwardSeveral(const uint8_t * const StartOfThisBlock, const uint8_t * const LastOfNextBlock, const unsigned int Length, const unsigned int Skip);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    RollingChecksum::GetChecksum()
	//		Purpose: Returns the checksum
	//		Created: 6/12/03
	//
	// --------------------------------------------------------------------------	
	inline uint32_t GetChecksum() const
	{
		return ((uint32_t)a) | (((uint32_t)b) << 16);
	}
	
	// Components, just in case they're handy
	inline uint16_t GetComponent1() const {return a;}
	inline uint16_t GetComponent2() const {return b;}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    RollingChecksum::GetComponentForHashing()
	//		Purpose: Return the 16 bit component used for hashing and/or quick checks
	//		Created: 6/12/03
	//
	// --------------------------------------------------------------------------
	inline uint16_t GetComponentForHashing() const
	{
		return b;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    RollingChecksum::ExtractHashingComponent(uint32_t)
	//		Purpose: Static. Given a full checksum, extract the component used in the hashing table.
	//		Created: 14/1/04
	//
	// --------------------------------------------------------------------------
	static inline uint16_t ExtractHashingComponent(const uint32_t Checksum)
	{
		return Checksum >> 16;
	}
	
private:
	uint16_t a;
	uint16_t b;
};

#endif // ROLLINGCHECKSUM__H

