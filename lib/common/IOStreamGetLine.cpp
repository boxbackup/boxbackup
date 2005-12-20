// --------------------------------------------------------------------------
//
// File
//		Name:    IOStreamGetLine.cpp
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "IOStreamGetLine.h"
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
//		Name:    IOStreamGetLine::IOStreamGetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
IOStreamGetLine::IOStreamGetLine(IOStream &Stream)
	: mrStream(Stream),
	  mLineNumber(0),
	  mBufferBegin(0),
	  mBytesInBuffer(0),
	  mPendingEOF(false),
	  mEOF(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::~IOStreamGetLine()
//		Purpose: Destructor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
IOStreamGetLine::~IOStreamGetLine()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::GetLine(std::string &, bool, int)
//		Purpose: Gets a line from the file, returning it in rOutput. If Preprocess is true, leading
//				 and trailing whitespace is removed, and comments (after #)
//				 are deleted.
//				 Returns true if a line is available now, false if retrying may get a line (eg timeout, signal),
//				 and exceptions if it's EOF.
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
bool IOStreamGetLine::GetLine(std::string &rOutput, bool Preprocess, int Timeout)
{
	// EOF?
	if(mEOF) {THROW_EXCEPTION(CommonException, GetLineEOF)}
	
	// Initialise string to stored into
	std::string r(mPendingString);
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
				r += c;
			}
			
			// Implicit line ending at EOF
			if(mBufferBegin >= mBytesInBuffer && mPendingEOF)
			{
				foundLineEnd = true;
			}
		}
		
		// Check size
		if(r.size() > IOSTREAMGETLINE_MAX_LINE_SIZE)
		{
			THROW_EXCEPTION(CommonException, GetLineTooLarge)
		}
		
		// Read more in?
		if(!foundLineEnd && mBufferBegin >= mBytesInBuffer && !mPendingEOF)
		{
			size_t bytes = mrStream.Read(mBuffer, sizeof(mBuffer), Timeout);
			
			// Adjust buffer info
			mBytesInBuffer = bytes;
			mBufferBegin = 0;
			
			// EOF / closed?
			if(!mrStream.StreamDataLeft())
			{
				mPendingEOF = true;
			}
			
			// No data returned?
			if(bytes == 0 && mrStream.StreamDataLeft())
			{
				// store string away
				mPendingString = r;
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

	if(!Preprocess)
	{
		rOutput = r;
		return true;
	}
	else
	{
		// Check for comment char, but char before must be whitespace
		size_t end = 0;
		size_t size = r.size();
		while(end < size)
		{
			if(r[end] == '#' && (end == 0 || (iw(r[end-1]))))
			{
				break;
			}
			end++;
		}
		
		// Remove whitespace
		size_t begin = 0;
		while(begin < size && iw(r[begin]))
		{
			begin++;
		}
		if(!iw(r[end])) end--;
		while(end > begin && iw(r[end]))
		{
			end--;
		}
		
		// Return a sub string
		rOutput = r.substr(begin, end - begin + 1);
		return true;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::DetachFile()
//		Purpose: Detaches the file handle, setting the file pointer correctly.
//				 Probably not good for sockets...
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
void IOStreamGetLine::DetachFile()
{
	// Adjust file pointer
	size_t bytesOver = mBytesInBuffer - mBufferBegin;
	ASSERT(bytesOver >= 0);
	if(bytesOver > 0)
	{
		mrStream.Seek(0 - bytesOver, IOStream::SeekType_Relative);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::IgnoreBufferedData(int)
//		Purpose: Ignore buffered bytes (effectively removing them from the
//				 beginning of the buffered data.)
//				 Cannot remove more bytes than are currently in the buffer.
//				 Be careful when this is used!
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void IOStreamGetLine::IgnoreBufferedData(size_t BytesToIgnore)
{
	size_t bytesInBuffer = mBytesInBuffer - mBufferBegin;
	if(BytesToIgnore > bytesInBuffer)
	{
		THROW_EXCEPTION(CommonException, IOStreamGetLineNotEnoughDataToIgnore)
	}
	mBufferBegin += BytesToIgnore;
}



