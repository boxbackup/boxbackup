// --------------------------------------------------------------------------
//
// File
//		Name:    RateLimitingStream.cpp
//		Purpose: Rate-limiting write-only wrapper around IOStreams
//		Created: 2011/01/11
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "Exception.h"
#include "RateLimitingStream.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    RateLimitingStream::RateLimitingStream(const char *, int, int)
//		Purpose: Constructor, set up buffer
//		Created: 2011/01/11
//
// --------------------------------------------------------------------------
RateLimitingStream::RateLimitingStream(IOStream& rSink, size_t targetBytesPerSecond)
: mrSink(rSink), mStartTime(GetCurrentBoxTime()), mTotalBytesRead(0),
  mTargetBytesPerSecond(targetBytesPerSecond)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    RateLimitingStream::Read(void *pBuffer, int NBytes,
//			 int Timeout)
//		Purpose: Reads bytes to the underlying stream at no more than
//			 a fixed rate
//		Created: 2011/01/11
//
// --------------------------------------------------------------------------
int RateLimitingStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	if(NBytes > 0 && (size_t)NBytes > mTargetBytesPerSecond)
	{
		// Limit to one second's worth of data for performance
		BOX_TRACE("Reducing read size from " << NBytes << " to " <<
			mTargetBytesPerSecond << " to smooth upload rate");
		NBytes = mTargetBytesPerSecond;
	}

	int bytesReadThisTime = mrSink.Read(pBuffer, NBytes, Timeout);

	// How many bytes we will have written after this write finishes?
	mTotalBytesRead += bytesReadThisTime;

	// When should it be completed by?
	box_time_t desiredFinishTime = mStartTime +
		SecondsToBoxTime(mTotalBytesRead / mTargetBytesPerSecond);

	// How long do we have to wait?
	box_time_t currentTime = GetCurrentBoxTime();
	int64_t waitTime = desiredFinishTime - currentTime;

	// How are we doing so far? (for logging only)
	box_time_t currentDuration = currentTime - mStartTime;

	// in case our timer is not very accurate, don't divide by zero on first pass
	if(currentDuration == 0)
	{
		BOX_TRACE("Current rate not yet known, sending immediately");
		return bytesReadThisTime;
	}
		
	uint64_t effectiveRateSoFar = (mTotalBytesRead * MICRO_SEC_IN_SEC_LL)
		/ currentDuration;

	if(waitTime > 0)
	{
		BOX_TRACE("Current rate " << effectiveRateSoFar <<
			" higher than desired rate " << mTargetBytesPerSecond <<
			", sleeping for " << BoxTimeToMilliSeconds(waitTime) <<
			" ms");
		ShortSleep(waitTime, false);
	}
	else
	{
		BOX_TRACE("Current rate " << effectiveRateSoFar <<
			" lower than desired rate " << mTargetBytesPerSecond <<
			", sending immediately (would have sent " <<
			(BoxTimeToMilliSeconds(-waitTime)) << " ms ago)");
	}

	return bytesReadThisTime;
}

