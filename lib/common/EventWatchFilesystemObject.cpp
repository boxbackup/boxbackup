// --------------------------------------------------------------------------
//
// File
//		Name:    EventWatchFilesystemObject.cpp
//		Purpose: WaitForEvent compatible object for watching directories
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "EventWatchFilesystemObject.h"
#include "autogen_CommonException.h"
#include "Logging.h"

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
#ifdef HAVE_KQUEUE
	: mDescriptor(::open(Filename, O_RDONLY /*O_EVTONLY*/, 0))
#endif
{
#ifdef HAVE_KQUEUE
	if(mDescriptor == -1)
	{
		BOX_ERROR("EventWatchFilesystemObject: "
			"Failed to open file '" << Filename << "': " <<
			strerror(errno));
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


#ifdef HAVE_KQUEUE
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

