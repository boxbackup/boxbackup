// --------------------------------------------------------------------------
//
// File
//		Name:    DiscardStream.cpp
//		Purpose: Discards data written to it
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "DiscardStream.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::DiscardStream()
//		Purpose: Constructor
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
DiscardStream::DiscardStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::~DiscardStream()
//		Purpose: Destructor
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
DiscardStream::~DiscardStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::Read(void *, int, int)
//		Purpose: As interface. Never reads anything :-)
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
int DiscardStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::BytesLeftToRead()
//		Purpose: As interface. Never anything to read.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
IOStream::pos_type DiscardStream::BytesLeftToRead()
{
	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::Write(void *, int)
//		Purpose: As interface. Dicards written data.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
void DiscardStream::Write(const void *pBuffer, int NBytes)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::GetPosition()
//		Purpose: As interface. Always returns 0.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
IOStream::pos_type DiscardStream::GetPosition() const
{
	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::Seek(pos_type, int)
//		Purpose: As interface. Does nothing.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
void DiscardStream::Seek(pos_type Offset, int SeekType)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::StreamDataLeft()
//		Purpose: As interface. Always returns false.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
bool DiscardStream::StreamDataLeft()
{
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    DiscardStream::StreamClosed()
//		Purpose: As interface. Always returns false.
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
bool DiscardStream::StreamClosed()
{
	return false;
}

