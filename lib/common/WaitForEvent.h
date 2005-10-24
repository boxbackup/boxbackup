// --------------------------------------------------------------------------
//
// File
//		Name:    WaitForEvent.h
//		Purpose: Generic waiting for events, using an efficient method (platform dependent)
//		Created: 9/3/04
//
// --------------------------------------------------------------------------

#ifndef WAITFOREVENT__H
#define WAITFOREVENT__H

#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	#include <sys/event.h>
	#include <sys/time.h>
#else
	#include <vector>
	#include <poll.h>
#endif

#include "CommonException.h"

#include "MemLeakFindOn.h"

class WaitForEvent
{
public:
	WaitForEvent(int Timeout = TimeoutInfinite);
	~WaitForEvent();
private:
	// No copying.
	WaitForEvent(const WaitForEvent &);
	WaitForEvent &operator=(const WaitForEvent &);
public:

	enum
	{
		TimeoutInfinite = -1
	};

	void SetTimeout(int Timeout = TimeoutInfinite);

	void *Wait();

#ifdef PLATFORM_KQUEUE_NOT_SUPPORTED
	typedef struct
	{
		int fd;
		short events;
		void *item;
	} ItemInfo;
#endif

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WaitForEvent::Add(const Type &, int)
	//		Purpose: Adds an event to the list of items to wait on. The flags are passed to the object.
	//		Created: 9/3/04
	//
	// --------------------------------------------------------------------------
	template<typename T>
	void Add(const T *pItem, int Flags = 0)
	{
		ASSERT(pItem != 0);
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
		struct kevent e;
		pItem->FillInKEvent(e, Flags);
		// Fill in extra flags to say what to do
		e.flags |= EV_ADD;
		e.udata = (void*)pItem;
		if(::kevent(mKQueue, &e, 1, NULL, 0, NULL) == -1)
		{
			THROW_EXCEPTION(CommonException, KEventErrorAdd)
		}
#else
		// Add item
		ItemInfo i;
		pItem->FillInPoll(i.fd, i.events, Flags);
		i.item = (void*)pItem;
		mItems.push_back(i);
		// Delete any pre-prepared poll info, as it's now out of date
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
	//		Name:    WaitForEvent::Remove(const Type &, int)
	//		Purpose: Removes an event from the list of items to wait on. The flags are passed to the object.
	//		Created: 9/3/04
	//
	// --------------------------------------------------------------------------
	template<typename T>
	void Remove(const T *pItem, int Flags = 0)
	{
		ASSERT(pItem != 0);
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
		struct kevent e;
		pItem->FillInKEvent(e, Flags);
		// Fill in extra flags to say what to do
		e.flags |= EV_DELETE;
		e.udata = (void*)pItem;
		if(::kevent(mKQueue, &e, 1, NULL, 0, NULL) == -1)
		{
			THROW_EXCEPTION(CommonException, KEventErrorRemove)
		}
#else
		if(mpPollInfo != 0)
		{
			::free(mpPollInfo);
			mpPollInfo = 0;
		}
		for(std::vector<ItemInfo>::iterator i(mItems.begin()); i != mItems.end(); ++i)
		{
			if(i->item == pItem)
			{
				mItems.erase(i);
				return;
			}
		}
#endif
	}

private:
#ifndef PLATFORM_KQUEUE_NOT_SUPPORTED
	int mKQueue;
	struct timespec mTimeout;
	struct timespec *mpTimeout;
#else
	int mTimeout;
	std::vector<ItemInfo> mItems;
	struct pollfd *mpPollInfo;
#endif
};

#include "MemLeakFindOff.h"

#endif // WAITFOREVENT__H


