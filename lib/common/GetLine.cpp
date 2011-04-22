// --------------------------------------------------------------------------
//
// File
//		Name:    GetLine.cpp
//		Purpose: Common base class for line based file descriptor reading
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "GetLine.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// utility whitespace function
inline bool iw(int c)
{
	return (c == ' ' || c == '\t' || c == '\v' || c == '\f'); // \r, \n are already excluded
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    GetLine::GetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
GetLine::GetLine()
: mLineNumber(0),
  mBufferBegin(0),
  mBytesInBuffer(0),
  mPendingEOF(false),
  mEOF(false)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    GetLine::GetLineInternal(std::string &, bool, int)
//		Purpose: Gets a line from the file, returning it in rOutput.
//			 If Preprocess is true, leading and trailing
//			 whitespace is removed, and comments (after #)  are
//			 deleted. Returns true if a line is available now,
//			 false if retrying may get a line (eg timeout,
//			 signal), and exceptions if it's EOF.
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
bool GetLine::GetLineInternal(std::string &rOutput, bool Preprocess,
	int Timeout)
{
	// EOF?
	if(mEOF) {THROW_EXCEPTION(CommonException, GetLineEOF)}
	
	// Initialise string to stored into
	rOutput = mPendingString;
	mPendingString.erase();

	bool foundLineEnd = false;

	while(!foundLineEnd && !mEOF)
	{
		// Use any bytes left in the buffer
		while(mBufferBegin < mBytesInBuffer)
		{
			int c = mBuffer[mBufferBegin++];
			if(c == '\r')
			{
				// Ignore nasty Windows line ending extra chars
			}
			else if(c == '\n')
			{
				// Line end!
				foundLineEnd = true;
				break;
			}
			else
			{
				// Add to string
				rOutput += c;
			}
			
			// Implicit line ending at EOF
			if(mBufferBegin >= mBytesInBuffer && mPendingEOF)
			{
				foundLineEnd = true;
			}
		}
		
		// Check size
		if(rOutput.size() > GETLINE_MAX_LINE_SIZE)
		{
			THROW_EXCEPTION(CommonException, GetLineTooLarge)
		}
		
		// Read more in?
		if(!foundLineEnd && mBufferBegin >= mBytesInBuffer && !mPendingEOF)
		{
			int bytes = ReadMore(Timeout);
			
			// Error?
			if(bytes == -1)
			{
				THROW_EXCEPTION(CommonException, OSFileError)
			}
			
			// Adjust buffer info
			mBytesInBuffer = bytes;
			mBufferBegin = 0;
			
			// No data returned?
			if(bytes == 0 && IsStreamDataLeft())
			{
			       // store string away
			       mPendingString = rOutput;
			       // Return false;
			       return false;
			}
		}
		
		// EOF?
		if(mPendingEOF && mBufferBegin >= mBytesInBuffer)
		{
			// File is EOF, and now we've depleted the buffer completely, so tell caller as well.
			mEOF = true;
		}
	}

	if(Preprocess)
	{
		// Check for comment char, but char before must be whitespace
		// end points to a gap between characters, may equal start if
		// the string to be extracted has zero length, and indexes the
		// first character not in the string (== length, or a # mark
		// or whitespace)
		int end = 0;
		int size = rOutput.size();
		while(end < size)
		{
			if(rOutput[end] == '#' && (end == 0 || (iw(rOutput[end-1]))))
			{
				break;
			}
			end++;
		}
		
		// Remove whitespace
		int begin = 0;
		while(begin < size && iw(rOutput[begin]))
		{
			begin++;
		}

		while(end > begin && end <= size && iw(rOutput[end-1]))
		{
			end--;
		}
		
		// Return a sub string
		rOutput = rOutput.substr(begin, end - begin);
	}

	return true;
}


