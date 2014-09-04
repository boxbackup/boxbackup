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

#ifdef WIN32
#	ifndef _WIN32_WINNT
#		define _WIN32_WINNT 0x0500
#	elif _WIN32_WINNT < 0x0500
#		error Timers require at least Windows 2000 headers
#	endif
#endif

#include <signal.h>
#include <cstring>

#include "Timer.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

std::vector<Timer*>* Timers::spTimers = NULL;
bool Timers::sRescheduleNeeded = false;

#define TIMER_ID "timer " << mName << " (" << this << ") "
#define TIMER_ID_OF(t) "timer " << (t).GetName() << " (" << &(t) << ")"

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
		// no init needed
	#else
		struct sigaction newact, oldact;
		newact.sa_handler = Timers::SignalHandler;
		newact.sa_flags = SA_RESTART;
		sigemptyset(&newact.sa_mask);
		if (::sigaction(SIGALRM, &newact, &oldact) != 0)
		{
			BOX_ERROR("Failed to install signal handler");
			THROW_EXCEPTION(CommonException, Internal);
		}
		ASSERT(oldact.sa_handler == 0);
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
void Timers::Cleanup(bool throw_exception_if_not_initialised)
{
	if (throw_exception_if_not_initialised)
	{
		ASSERT(spTimers);
		if (!spTimers)
		{
			BOX_ERROR("Tried to clean up timers when not initialised!");
			return;
		}
	}
	else
	{
		if (!spTimers)
		{
			return;
		}
	}
	
	#if defined WIN32 && ! defined PLATFORM_CYGWIN
		// no cleanup needed
	#else
		struct itimerval timeout;
		memset(&timeout, 0, sizeof(timeout));

		int result = ::setitimer(ITIMER_REAL, &timeout, NULL);
		ASSERT(result == 0);

		struct sigaction newact, oldact;
		newact.sa_handler = SIG_DFL;
		newact.sa_flags = SA_RESTART;
		sigemptyset(&(newact.sa_mask));
		if (::sigaction(SIGALRM, &newact, &oldact) != 0)
		{
			BOX_ERROR("Failed to remove signal handler");
			THROW_EXCEPTION(CommonException, Internal);
		}
		ASSERT(oldact.sa_handler == Timers::SignalHandler);
	#endif // WIN32 && !PLATFORM_CYGWIN

	spTimers->clear();
	delete spTimers;
	spTimers = NULL;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::AssertInitialised()
//		Purpose: Throw an assertion error if timers are not ready
//			 NOW. It's a common mistake (for me) when writing
//			 tests to forget to initialise timers first.
//		Created: 15/05/2014
//
// --------------------------------------------------------------------------

void Timers::AssertInitialised()
{
	if (!spTimers)
	{
		THROW_EXCEPTION(CommonException, TimersNotInitialised);
	}
	ASSERT(spTimers);
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
	BOX_TRACE(TIMER_ID_OF(rTimer) " added to global queue, rescheduling");
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
	ASSERT(&rTimer);

	if(!spTimers)
	{
		BOX_WARNING(TIMER_ID_OF(rTimer) " was still active after "
			"timer subsystem was cleaned up, already removed.");
		return;
	}

	ASSERT(spTimers);
	BOX_TRACE(TIMER_ID_OF(rTimer) " removed from global queue, rescheduling");

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

void Timers::RequestReschedule()
{
	sRescheduleNeeded = true;
}

void Timers::RescheduleIfNeeded()
{
	if (sRescheduleNeeded) 
	{
		Reschedule();
	}
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
		struct sigaction oldact;
		if (::sigaction(SIGALRM, NULL, &oldact) != 0)
		{
			BOX_ERROR("Failed to check signal handler");
			THROW_EXCEPTION(CommonException, Internal)
		}

		ASSERT(oldact.sa_handler == Timers::SignalHandler);

		if (oldact.sa_handler != Timers::SignalHandler)
		{
			BOX_ERROR("Signal handler was " <<
				(void *)oldact.sa_handler << 
				", expected " <<
				(void *)Timers::SignalHandler);
			THROW_EXCEPTION(CommonException, Internal)
		}
	#endif

	// Clear the reschedule-needed flag to false before we start.
	// If a timer event occurs while we are scheduling, then we
	// may or may not need to reschedule again, but this way
	// we will do it anyway.
	sRescheduleNeeded = false;

#ifdef WIN32
	// win32 timers need no management
#else
	box_time_t timeNow = GetCurrentBoxTime();
	int64_t timeToNextEvent;
	std::string nameOfNextEvent;

	// scan for, trigger and remove expired timers. Removal requires
	// us to restart the scan each time, due to std::vector semantics.
	bool restart = true;
	while (restart)
	{
		restart = false;
		timeToNextEvent = 0;

		for (std::vector<Timer*>::iterator i = spTimers->begin();
			i != spTimers->end(); i++)
		{
			Timer& rTimer = **i;
			int64_t timeToExpiry = rTimer.GetExpiryTime() - timeNow;
		
			if (timeToExpiry <= 0)
			{
				BOX_TRACE(TIMER_ID_OF(**i) " has expired, "
					"triggering " <<
					BOX_FORMAT_MICROSECONDS(-timeToExpiry) <<
					" late");
				rTimer.OnExpire();
				spTimers->erase(i);
				restart = true;
				break;
			}
			else
			{
				/*
				BOX_TRACE(TIMER_ID_OF(**i) " has not expired, "
					"triggering in " <<
					FORMAT_MICROSECONDS(timeToExpiry) <<
					" seconds");
				*/
			}

			if (timeToNextEvent == 0 || timeToNextEvent > timeToExpiry)
			{
				timeToNextEvent = timeToExpiry;
				nameOfNextEvent = rTimer.GetName();
			}
		}
	}

	if (timeToNextEvent == 0)
	{
		BOX_TRACE("timer: no more events, going to sleep.");
	}
	else
	{
		BOX_TRACE("timer: next event: " << nameOfNextEvent << " at " <<
			FormatTime(timeNow + timeToNextEvent, false, true));
	}

	struct itimerval timeout;
	memset(&timeout, 0, sizeof(timeout));
	
	timeout.it_value.tv_sec  = BoxTimeToSeconds(timeToNextEvent);
	timeout.it_value.tv_usec = (int)
		(BoxTimeToMicroSeconds(timeToNextEvent)
		% MICRO_SEC_IN_SEC);

	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
	{
		BOX_ERROR("Failed to initialise system timer\n");
		THROW_EXCEPTION(CommonException, Internal)
	}
#endif
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
void Timers::SignalHandler(int unused)
{
	// ASSERT(spTimers);
	Timers::RequestReschedule();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Timer(size_t timeoutMillis,
//			 const std::string& rName)
//		Purpose: Standard timer constructor, takes a timeout in
//			 seconds from now, and an optional name for
//			 logging purposes.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

Timer::Timer(size_t timeoutMillis, const std::string& rName)
: mExpires(0),
  mExpired(false),
  mName(rName)
#ifdef WIN32
, mTimerHandle(INVALID_HANDLE_VALUE)
#endif
{
	Set(timeoutMillis, true /* isInit */);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Start()
//		Purpose: This internal function recalculates the remaining
//			 time (timeout) from the expiry time, and then calls
//			 Start(timeoutMillis).
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

void Timer::Start()
{
	box_time_t timeNow = GetCurrentBoxTime();
	int64_t timeToExpiry = mExpires - timeNow;

	if (timeToExpiry <= 0)
	{
		BOX_WARNING(TIMER_ID << "fudging expiry from -" <<
			BOX_FORMAT_MICROSECONDS(-timeToExpiry))
		timeToExpiry = 1;
	}

	Start(timeToExpiry / MICRO_SEC_IN_MILLI_SEC);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Start(int64_t timeoutMillis)
//		Purpose: This internal function adds this timer to the global
//			 timer list, and on Windows it initialises an OS
//			 TimerQueue timer for it.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

void Timer::Start(int64_t timeoutMillis)
{
	ASSERT(mExpires != 0);
	Timers::Add(*this);

#ifdef WIN32
	// only call me once!
	ASSERT(mTimerHandle == INVALID_HANDLE_VALUE);

	// Windows XP always seems to fire timers up to 20 ms late,
	// at least on my test laptop. Not critical in practice, but our
	// tests are precise enough that they will fail if we don't
	// correct for it.
	timeoutMillis -= 20;
	
	// Set a system timer to call our timer routine
	if (CreateTimerQueueTimer(&mTimerHandle, NULL, TimerRoutine,
		(PVOID)this, timeoutMillis, 0, WT_EXECUTEINTIMERTHREAD)
		== FALSE)
	{
		BOX_ERROR(TIMER_ID "failed to create timer: " <<
			GetErrorMessage(GetLastError()));
		mTimerHandle = INVALID_HANDLE_VALUE;
	}
	else
	{
		BOX_INFO(TIMER_ID << "set for " << timeoutMillis << " ms");
	}
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Stop()
//		Purpose: This internal function removes us from the global
//			 list of timers, resets our expiry time, and on
//			 Windows it deletes the associated OS TimerQueue timer.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

void Timer::Stop()
{
	if (mExpires != 0)
	{
		Timers::Remove(*this);
	}

#ifdef WIN32
	if (mTimerHandle != INVALID_HANDLE_VALUE)
	{
		if (DeleteTimerQueueTimer(NULL, mTimerHandle,
			INVALID_HANDLE_VALUE) == FALSE)
		{
			BOX_ERROR(TIMER_ID "failed to delete timer: " <<
				GetErrorMessage(GetLastError()));
		}
		mTimerHandle = INVALID_HANDLE_VALUE;
	}
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::~Timer()
//		Purpose: Destructor for Timer objects.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

Timer::~Timer()
{
	#ifndef BOX_RELEASE_BUILD
	BOX_TRACE(TIMER_ID "destroyed");
	#endif

	Stop();
}

void Timer::LogAssignment(const Timer &From)
{
	#ifndef BOX_RELEASE_BUILD
	BOX_TRACE(TIMER_ID "initialised from " << TIMER_ID_OF(From));

	if (From.mExpired)
	{
		BOX_TRACE(TIMER_ID "already expired, will not fire");
	}
	else if (From.mExpires == 0)
	{
		BOX_TRACE(TIMER_ID "has no expiry time, will not fire");
	}
	else
	{
		BOX_TRACE(TIMER_ID "will fire at " <<
			FormatTime(From.mExpires, false, true));
	}
	#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Timer(Timer& rToCopy)
//		Purpose: Copy constructor for Timer objects. Creates a new
//			 timer that will trigger at the same time as the
//			 original. The original will usually be discarded.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

Timer::Timer(const Timer& rToCopy)
: mExpires(rToCopy.mExpires),
  mExpired(rToCopy.mExpired),
  mName(rToCopy.mName)
#ifdef WIN32
, mTimerHandle(INVALID_HANDLE_VALUE)
#endif
{
	LogAssignment(rToCopy);

	if (!mExpired && mExpires != 0)
	{
		Start();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::operator=(const Timer& rToCopy)
//		Purpose: Assignment operator for Timer objects. Works
//			 exactly the same as the copy constructor, except
//			 that if the receiving timer is already running,
//			 it is stopped first.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

Timer& Timer::operator=(const Timer& rToCopy)
{
	LogAssignment(rToCopy);

	Stop();

	mExpires = rToCopy.mExpires;
	mExpired = rToCopy.mExpired;
	mName    = rToCopy.mName;

	if (!mExpired && mExpires != 0)
	{
		Start();
	}

	return *this;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Reset(size_t timeoutMillis)
//		Purpose: Simple reset operation for an existing Timer. Avoids
//			 the need to create a temporary timer just to modify
//			 an existing one.
//		Created: 17/11/2012
//
// --------------------------------------------------------------------------

void Timer::Reset(size_t timeoutMillis)
{
	Set(timeoutMillis, false /* isInit */);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::Reset(size_t timeoutMillis)
//		Purpose: Internal set/reset operation for an existing Timer.
//			 Shared by constructor and Reset().
//		Created: 17/11/2012
//
// --------------------------------------------------------------------------

void Timer::Set(size_t timeoutMillis, bool isInit)
{
	Stop();
	mExpired = false;
	
	if (timeoutMillis == 0)
	{
		mExpires = 0;
	}
	else
	{
		mExpires = GetCurrentBoxTime() + 
			MilliSecondsToBoxTime(timeoutMillis);
	}

	#ifndef BOX_RELEASE_BUILD
	if (timeoutMillis == 0)
	{
		BOX_TRACE(TIMER_ID << (isInit ? "initialised" : "reset") <<
			" for " << timeoutMillis << " ms, will not fire");
	}
	else
	{
		BOX_TRACE(TIMER_ID << (isInit ? "initialised" : "reset") <<
			" for " << timeoutMillis << " ms, to fire at " <<
			FormatTime(mExpires, false, true));
	}
	#endif

	if (mExpires != 0)
	{
		Start(timeoutMillis);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::OnExpire()
//		Purpose: Method called by Timers::Reschedule (on Unixes)
//			 on next poll after timer expires, or from
//			 Timer::TimerRoutine (on Windows) from a separate
//			 thread managed by the OS. Marks the timer as
//			 expired for future reference.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

void Timer::OnExpire()
{
	#ifndef BOX_RELEASE_BUILD
	BOX_TRACE(TIMER_ID "fired");
	#endif

	mExpired = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Timer::TimerRoutine(PVOID lpParam,
//			 BOOLEAN TimerOrWaitFired)
//		Purpose: Static method called by the Windows OS when a
//			 TimerQueue timer expires.
//		Created: 27/07/2008
//
// --------------------------------------------------------------------------

#ifdef WIN32
VOID CALLBACK Timer::TimerRoutine(PVOID lpParam,
	BOOLEAN TimerOrWaitFired)
{
	Timer* pTimer = (Timer*)lpParam;
	pTimer->OnExpire();
	// is it safe to write to write debug output from a timer?
	// e.g. to write to the Event Log?
}
#endif
