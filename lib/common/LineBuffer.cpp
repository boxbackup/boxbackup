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

#include "BoxTime.h"
#include "Exception.h"
#include "LineBuffer.h"

#include "MemLeakFindOn.h"

// utility whitespace function
inline bool iw(int c)
{
	return (c == ' ' || c == '\t' || c == '\v' || c == '\f'); // \r, \n are already excluded
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    LineBuffer::LineBuffer()
//		Purpose: Constructor
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
LineBuffer::LineBuffer()
: mLineNumber(0),
  mBufferBegin(0),
  mBytesInBuffer(0),
  mPendingEOF(false),
  mEOF(false)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    LineBuffer::GetLine(bool, int)
//		Purpose: Gets a line from the input using ReadMore, and
//		         returns it. If Preprocess is true, leading and
//		         trailing whitespace is removed, and comments (after
//		         #) are deleted. Throws exceptions on timeout, signal
//		         received and EOF.
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
std::string LineBuffer::GetLine(bool preprocess, int timeout)
{
	// EOF?
	if(mEOF)
	{
		THROW_EXCEPTION(CommonException, GetLineEOF);
	}
	
	// Initialise string to stored into
	std::string output = mPendingString;
	mPendingString.erase();

	box_time_t start_time = GetCurrentBoxTime();
	box_time_t remaining_time = MilliSecondsToBoxTime(timeout);
	box_time_t end_time = start_time + remaining_time;
	bool found_line_end = false;

	while(!found_line_end && !mEOF)
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
				found_line_end = true;
				break;
			}
			else
			{
				// Add to string
				output += c;
			}
			
			// Implicit line ending at EOF
			if(mBufferBegin >= mBytesInBuffer && mPendingEOF)
			{
				found_line_end = true;
			}
		}
		
		// Check size
		if(output.size() > GETLINE_MAX_LINE_SIZE)
		{
			THROW_EXCEPTION(CommonException, GetLineTooLarge);
		}

		if(timeout != IOStream::TimeOutInfinite)
		{
			// Update remaining time, and if we have run out and not yet found EOL, then
			// stash what we've read so far, and return false. (If the timeout is infinite,
			// the only way out is EOL or EOF or a signal).
			remaining_time = end_time - GetCurrentBoxTime();
			if(!found_line_end && remaining_time < 0)
			{
			       mPendingString = output;
			       THROW_EXCEPTION(CommonException, IOStreamTimedOut);
			}
		}

		// Read more in?
		if(!found_line_end && mBufferBegin >= mBytesInBuffer && !mPendingEOF)
		{
			int64_t read_timeout_ms;
			if(timeout == IOStream::TimeOutInfinite)
			{
				read_timeout_ms = IOStream::TimeOutInfinite;
			}
			else
			{
				// We should have exited above, if remaining_time < 0.
				ASSERT(remaining_time >= 0);
				read_timeout_ms = BoxTimeToMilliSeconds(remaining_time);
			}

			int bytes = ReadMore(read_timeout_ms);
			
			// Error?
			if(bytes == -1)
			{
				THROW_EXCEPTION(CommonException, OSFileError);
			}
			
			// Adjust buffer info
			mBytesInBuffer = bytes;
			mBufferBegin = 0;

			// No data returned? This could mean that (1) we reached EOF:
			if(bytes == 0)
			{
				// This could mean that (1) we reached EOF:
				if(!IsStreamDataLeft())
				{
					mPendingEOF = true;
					// Exception will be thrown at next start of loop.
				}
				// Or (2) we ran out of time waiting to read enough data
				// (signals throw an exception in SocketStream instead).
				else
				{
					// Store string away, ready for when the caller asks us again.
					mPendingString = output;
					THROW_EXCEPTION(CommonException, IOStreamTimedOut);
				}
			}
		}
		
		// EOF?
		if(mPendingEOF && mBufferBegin >= mBytesInBuffer)
		{
			// File is EOF, and now we've depleted the buffer completely, so tell caller as well.
			mEOF = true;
		}
	}

	if(preprocess)
	{
		// Check for comment char, but char before must be whitespace
		// end points to a gap between characters, may equal start if
		// the string to be extracted has zero length, and indexes the
		// first character not in the string (== length, or a # mark
		// or whitespace)
		int end = 0;
		int size = output.size();
		while(end < size)
		{
			if(output[end] == '#' && (end == 0 || (iw(output[end-1]))))
			{
				break;
			}
			end++;
		}
		
		// Remove whitespace
		int begin = 0;
		while(begin < size && iw(output[begin]))
		{
			begin++;
		}

		while(end > begin && end <= size && iw(output[end-1]))
		{
			end--;
		}
		
		// Return a sub string
		output = output.substr(begin, end - begin);
	}

	return output;
}


