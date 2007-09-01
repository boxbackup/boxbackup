// --------------------------------------------------------------------------
//
// File
//		Name:    Timer.cpp
//		Purpose: Generic timers which execute arbitrary code when
//			 they expire.
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <signal.h>

#include "Timer.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

std::vector<Timer*>* Timers::spTimers = NULL;
bool Timers::sRescheduleNeeded = false;

typedef void (*sighandler_t)(int);

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Init()
//		Purpose: Initialise timers, prepare signal handler
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::Init()
{
	ASSERT(!spTimers);
	
	#if defined WIN32 && ! defined PLATFORM_CYGWIN
		// no support for signals at all
		InitTimer();
		SetTimerHandler(Timers::SignalHandler);
	#else
		sighandler_t oldHandler = ::sigset(SIGALRM, 
			Timers::SignalHandler);
		ASSERT(oldHandler == 0);
	#endif // WIN32 && !PLATFORM_CYGWIN
	
	spTimers = new std::vector<Timer*>;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Cleanup()
//		Purpose: Clean up timers, stop signal handler
//		Created: 6/11/2006
//
// --------------------------------------------------------------------------
void Timers::Cleanup()
{
	ASSERT(spTimers);
	
	#if defined WIN32 && ! defined PLATFORM_CYGWIN
		// no support for signals at all
		FiniTimer();
		SetTimerHandler(NULL);
	#else
		struct itimerval timeout;
		memset(&timeout, 0, sizeof(timeout));

		int result = ::setitimer(ITIMER_REAL, &timeout, NULL);
		ASSERT(result == 0);

		sighandler_t oldHandler = ::sigset(SIGALRM, NULL);
		ASSERT(oldHandler == Timers::SignalHandler);
	#endif // WIN32 && !PLATFORM_CYGWIN

	spTimers->clear();
	delete spTimers;
	spTimers = NULL;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Add(Timer&)
//		Purpose: Add a new timer to the set, and reschedule next wakeup
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::Add(Timer& rTimer)
{
	ASSERT(spTimers);
	ASSERT(&rTimer);
	spTimers->push_back(&rTimer);
	Reschedule();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Remove(Timer&)
//		Purpose: Removes the timer from the set (preventing it from
//			 being called) and reschedule next wakeup
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::Remove(Timer& rTimer)
{
	ASSERT(spTimers);
	ASSERT(&rTimer);

	bool restart = true;
	while (restart)
	{
		restart = false;

		for (std::vector<Timer*>::iterator i = spTimers->begin();
			i != spTimers->end(); i++)
		{
			if (&rTimer == *i)
			{
				spTimers->erase(i);
				restart = true;
				break;
			}
		}
	}
		
	Reschedule();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Reschedule()
//		Purpose: Recalculate when the next wakeup is due
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::Reschedule()
{
	ASSERT(spTimers);
	if (spTimers == NULL)
	{
		THROW_EXCEPTION(CommonException, Internal)
	}

	#ifndef WIN32
	void (*oldhandler)(int) = ::sigset(SIGALRM, Timers::SignalHandler);
	if (oldhandler != Timers::SignalHandler)
	{
		printf("Signal handler was %p, expected %p\n", 
			oldhandler, Timers::SignalHandler);
		THROW_EXCEPTION(CommonException, Internal)
	}
	#endif

	// Clear the reschedule-needed flag to false before we start.
	// If a timer event occurs while we are scheduling, then we
	// may or may not need to reschedule again, but this way
	// we will do it anyway.
	sRescheduleNeeded = false;

	box_time_t timeNow = GetCurrentBoxTime();

	// scan for, trigger and remove expired timers. Removal requires
	// us to restart the scan each time, due to std::vector semantics.
	bool restart = true;
	while (restart)
	{
		restart = false;

		for (std::vector<Timer*>::iterator i = spTimers->begin();
			i != spTimers->end(); i++)
		{
			Timer& rTimer = **i;
			int64_t timeToExpiry = rTimer.GetExpiryTime() - timeNow;
		
			if (timeToExpiry <= 0)
			{
				BOX_TRACE((int)(timeNow / 1000000) << "." <<
					(int)(timeNow % 1000000) <<
					": timer " << *i << " has expired, "
					"triggering it");
				rTimer.OnExpire();
				spTimers->erase(i);
				restart = true;
				break;
			}
			else
			{
				BOX_TRACE((int)(timeNow / 1000000) << "." <<
					(int)(timeNow % 1000000) <<
					": timer " << *i << " has not "
					"expired, triggering in " <<
					(int)(timeToExpiry / 1000000) << "." <<
					(int)(timeToExpiry % 1000000) <<
					" seconds");
			}
		}
	}

	// Now the only remaining timers should all be in the future.
	// Scan to find the next one to fire (earliest deadline).
			
	int64_t timeToNextEvent = 0;

	for (std::vector<Timer*>::iterator i = spTimers->begin();
		i != spTimers->end(); i++)
	{
		Timer& rTimer = **i;
		int64_t timeToExpiry = rTimer.GetExpiryTime() - timeNow;

		if (timeToExpiry <= 0)
		{
			timeToExpiry = 1;
		}
		
		if (timeToNextEvent == 0 || timeToNextEvent > timeToExpiry)
		{
			timeToNextEvent = timeToExpiry;
		}
	}
	
	ASSERT(timeToNextEvent >= 0);
	
	struct itimerval timeout;
	memset(&timeout, 0, sizeof(timeout));
	
	timeout.it_value.tv_sec  = BoxTimeToSeconds(timeToNextEvent);
	timeout.it_value.tv_usec = (int)
		(BoxTimeToMicroSeconds(timeToNextEvent) % MICRO_SEC_IN_SEC);

	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
	{
		BOX_ERROR("Failed to initialise timer\n");
		THROW_EXCEPTION(CommonException, Internal)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::SignalHandler(unused)
//		Purpose: Called as signal handler. Nothing is safe in a signal
//			 handler, not even traversing the list of timers, so
//			 just request a reschedule in future, which will do
//			 that for us, and trigger any expired timers at that
//			 time.
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::SignalHandler(int iUnused)
{
	// ASSERT(spTimers);
	Timers::RequestReschedule();
}

Timer::Timer(size_t timeoutSecs)
: mExpires(GetCurrentBoxTime() + SecondsToBoxTime(timeoutSecs)),
  mExpired(false)
{
	#if !defined NDEBUG && !defined WIN32
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (timeoutSecs == 0)
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised for " <<
			timeoutSecs << " secs, will not fire");
	}
	else
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised for " <<
			timeoutSecs << " secs, to fire at " <<
			(int)(mExpires / 1000000) << "." <<
			(int)(mExpires % 1000000));
	}
	#endif

	if (timeoutSecs == 0)
	{
		mExpires = 0;
	}
	else
	{
		Timers::Add(*this);
	}
}

Timer::~Timer()
{
	#if !defined NDEBUG && !defined WIN32
	struct timeval tv;
	gettimeofday(&tv, NULL);
	BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
		": timer " << this << " destroyed");
	#endif

	Timers::Remove(*this);
}

Timer::Timer(const Timer& rToCopy)
: mExpires(rToCopy.mExpires),
  mExpired(rToCopy.mExpired)
{
	#if !defined NDEBUG && !defined WIN32
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (mExpired)
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << ", already expired, will not fire");
	}
	else if (mExpires == 0)
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << ", no expiry, will not fire");
	}
	else
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << " to fire at " <<
			(int)(mExpires / 1000000) << "." <<
			(int)(mExpires % 1000000));
	}
	#endif

	if (!mExpired && mExpires != 0)
	{
		Timers::Add(*this);
	}
}

Timer& Timer::operator=(const Timer& rToCopy)
{
	#if !defined NDEBUG && !defined WIN32
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (rToCopy.mExpired)
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << ", already expired, will not fire");
	}
	else if (rToCopy.mExpires == 0)
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << ", no expiry, will not fire");
	}
	else
	{
		BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
			": timer " << this << " initialised from timer " <<
			&rToCopy << " to fire at " <<
			(int)(rToCopy.mExpires / 1000000) << "." <<
			(int)(rToCopy.mExpires % 1000000));
	}
	#endif

	Timers::Remove(*this);
	mExpires = rToCopy.mExpires;
	mExpired = rToCopy.mExpired;
	if (!mExpired && mExpires != 0)
	{
		Timers::Add(*this);
	}
	return *this;
}

void Timer::OnExpire()
{
	#if !defined NDEBUG && !defined WIN32
	struct timeval tv;
	gettimeofday(&tv, NULL);
	BOX_TRACE(tv.tv_sec << "." << tv.tv_usec <<
		": timer " << this << " fired");
	#endif

	mExpired = true;
}
