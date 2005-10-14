// --------------------------------------------------------------------------
//
// File
//		Name:    EventWatchFilesystemObject.h
//		Purpose: WaitForEvent compatible object for watching directories
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#ifndef EVENTWATCHFILESYSTEMOBJECT__H
#define EVENTWATCHFILESYSTEMOBJECT__H

#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	#include <sys/event.h>
#endif


// --------------------------------------------------------------------------
//
// Class
//		Name:    EventWatchFilesystemObject
//		Purpose: WaitForEvent compatible object for watching files and directories
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
class EventWatchFilesystemObject
{
public:
	EventWatchFilesystemObject(const char *Filename);
	~EventWatchFilesystemObject();
	EventWatchFilesystemObject(const EventWatchFilesystemObject &rToCopy);
private:
	// Assignment not allowed
	EventWatchFilesystemObject &operator=(const EventWatchFilesystemObject &);
public:

#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	void FillInKEvent(struct kevent &rEvent, int Flags = 0) const;
#else
	void FillInPoll(int &fd, short &events, int Flags = 0) const;
#endif

private:
	int mDescriptor;
};

#endif // EventWatchFilesystemObject__H

