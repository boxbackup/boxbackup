// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    WaitForEvent.cpp
//		Purpose: Generic waiting for events, using an efficient method (platform dependent)
//		Created: 9/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "WaitForEvent.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    WaitForEvent::WaitForEvent()
//		Purpose: Constructor
//		Created: 9/3/04
//
// --------------------------------------------------------------------------
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
WaitForEvent::WaitForEvent(int Timeout)
	: mKQueue(::kqueue()),
	  mpTimeout(0)
{
	if(mKQueue == -1)
	{
		THROW_EXCEPTION(CommonException, CouldNotCreateKQueue)
	}

	// Set the choosen timeout
	SetTimeout(Timeout);
}
#else
WaitForEvent::WaitForEvent(int Timeout)
	: mTimeout(Timeout),
	  mpPollInfo(0)
{
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    WaitForEvent::~WaitForEvent()
//		Purpose: Destructor
//		Created: 9/3/04
//
// --------------------------------------------------------------------------
WaitForEvent::~WaitForEvent()
{
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	::close(mKQueue);
	mKQueue = -1;
#else
	if(mpPollInfo != 0)
	{
		::free(mpPollInfo);
		mpPollInfo = 0;
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WaitForEvent::SetTimeout
//		Purpose: Sets the timeout for future wait calls
//		Created: 9/3/04
//
// --------------------------------------------------------------------------
void WaitForEvent::SetTimeout(int Timeout)
{
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	// Generate timeout
	if(Timeout != TimeoutInfinite)
	{
		mTimeout.tv_sec = Timeout / 1000;
		mTimeout.tv_nsec = (Timeout % 1000) * 1000000;
	}
	
	// Infinite or not?
	mpTimeout = (Timeout != TimeoutInfinite)?(&mTimeout):(NULL);
#else
	mTimeout = Timeout;
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WaitForEvent::Wait(int)
//		Purpose: Wait for an event to take place. Returns a pointer to the object
//				 which has been signalled, or returns 0 for the timeout condition.
//				 Timeout specified in milliseconds.
//		Created: 9/3/04
//
// --------------------------------------------------------------------------
void *WaitForEvent::Wait()
{
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	// Event return structure
	struct kevent e;
	::memset(&e, 0, sizeof(e));
	
	switch(::kevent(mKQueue, NULL, 0, &e, 1, mpTimeout))
	{
	case 0:
		// Timeout
		return 0;
		break;

	case 1:
		// Event happened!
		return e.udata;
		break;
		
	default:
		// Interrupted system calls aren't an error, just equivalent to a timeout
		if(errno != EINTR)
		{
			THROW_EXCEPTION(CommonException, KEventErrorWait)
		}
		return 0;
		break;
	}
#else
	// Use poll() instead.
	// Need to build the structures?
	if(mpPollInfo == 0)
	{
		// Yes...
		mpPollInfo = (struct pollfd *)::malloc((sizeof(struct pollfd) * mItems.size()) + 4);
		if(mpPollInfo == 0)
		{
			throw std::bad_alloc();
		}
		
		// Build...
		for(unsigned int l = 0; l < mItems.size(); ++l)
		{
			mpPollInfo[l].fd = mItems[l].fd;
			mpPollInfo[l].events = mItems[l].events;
			mpPollInfo[l].revents = 0;
		}
	}
	
	// Make sure everything is reset (don't really have to do this, but don't trust the OS)
	for(unsigned int l = 0; l < mItems.size(); ++l)
	{
		mpPollInfo[l].revents = 0;
	}
	
	// Poll!
	switch(::poll(mpPollInfo, mItems.size(), mTimeout))
	{
	case -1:
		// Interrupted system calls aren't an error, just equivalent to a timeout
		if(errno != EINTR)
		{
			THROW_EXCEPTION(CommonException, KEventErrorWait)
		}
		return 0;
		break;
	case 0:	// timed out
		return 0;
		break;
	default:	// got some thing...
		// control flows on...
		break;
	}
	
	// Find the item which was ready
	for(unsigned int s = 0; s < mItems.size(); ++s)
	{
		if(mpPollInfo[s].revents & POLLIN)
		{
			return mItems[s].item;
			break;
		}
	}
#endif

	return 0;
}

