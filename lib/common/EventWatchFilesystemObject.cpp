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
//		Name:    EventWatchFilesystemObject.cpp
//		Purpose: WaitForEvent compatible object for watching directories
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <fcntl.h>
#include <unistd.h>

#include "EventWatchFilesystemObject.h"
#include "autogen_CommonException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    EventWatchFilesystemObject::EventWatchFilesystemObject(const char *)
//		Purpose: Constructor -- opens the file object
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
EventWatchFilesystemObject::EventWatchFilesystemObject(const char *Filename)
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	: mDescriptor(::open(Filename, O_RDONLY /*O_EVTONLY*/, 0))
#endif
{
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	if(mDescriptor == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
#else
	THROW_EXCEPTION(CommonException, KQueueNotSupportedOnThisPlatform)
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    EventWatchFilesystemObject::~EventWatchFilesystemObject()
//		Purpose: Destructor
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
EventWatchFilesystemObject::~EventWatchFilesystemObject()
{
	if(mDescriptor != -1)
	{
		::close(mDescriptor);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    EventWatchFilesystemObject::EventWatchFilesystemObject(const EventWatchFilesystemObject &)
//		Purpose: Copy constructor
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
EventWatchFilesystemObject::EventWatchFilesystemObject(const EventWatchFilesystemObject &rToCopy)
	: mDescriptor(::dup(rToCopy.mDescriptor))
{
	if(mDescriptor == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
}


#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
// --------------------------------------------------------------------------
//
// Function
//		Name:    EventWatchFilesystemObject::FillInKEvent(struct kevent &, int)
//		Purpose: For WaitForEvent
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
void EventWatchFilesystemObject::FillInKEvent(struct kevent &rEvent, int Flags) const
{
	EV_SET(&rEvent, mDescriptor, EVFILT_VNODE, EV_CLEAR, NOTE_DELETE | NOTE_WRITE, 0, (void*)this);
}
#else
void EventWatchFilesystemObject::FillInPoll(int &fd, short &events, int Flags) const
{
	THROW_EXCEPTION(CommonException, KQueueNotSupportedOnThisPlatform)
}
#endif

