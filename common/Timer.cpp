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

#include "MemLeakFindOn.h"

std::vector<Timer*>* Timers::spTimers = NULL;

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void TimerSigHandler(int)
//		Purpose: Signal handler, notifies Timers class
//		Created: 19/3/04
//
// --------------------------------------------------------------------------
static void TimerSigHandler(int iUnused)
{
	Timers::Signal();	
}

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
	
	#ifdef PLATFORM_CYGWIN
		ASSERT(::signal(SIGALRM, TimerSigHandler) == 0);
	#elif defined WIN32
		// no support for signals at all
		InitTimer();
		SetTimerHandler(TimerSigHandler);
	#else
		ASSERT(::signal(SIGALRM, TimerSigHandler) == 0);
	#endif // PLATFORM_CYGWIN
	
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
	
	#ifdef PLATFORM_CYGWIN
		ASSERT(::signal(SIGALRM, NULL) == TimerSigHandler);
	#elif defined WIN32
		// no support for signals at all
		SetTimerHandler(NULL);
		FiniTimer();
	#else
		ASSERT(::signal(SIGALRM, NULL) == TimerSigHandler);
	#endif // PLATFORM_CYGWIN

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

	box_time_t timeNow = GetCurrentBoxTime();
	box_time_t timeToNextEvent = 0;
	
	for (std::vector<Timer*>::iterator i = spTimers->begin();
		i != spTimers->end(); i++)
	{
		Timer& rTimer = **i;
		ASSERT(!rTimer.HasExpired());
		
		box_time_t timeToExpiry = rTimer.GetExpiryTime() - timeNow;
		
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

#ifdef PLATFORM_CYGWIN
	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
#else
	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
#endif // PLATFORM_CYGWIN
	{
		TRACE0("WARNING: couldn't initialise timer\n");
		THROW_EXCEPTION(CommonException, Internal)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static void Timers::Signal()
//		Purpose: Called by signal handler. Signals any timers which
//			 are due or overdue, removes them from the set,
//			 and reschedules next wakeup.
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------
void Timers::Signal()
{
	ASSERT(spTimers);

	box_time_t timeNow = GetCurrentBoxTime();

	std::vector<Timer*> timersCopy = *spTimers;
	
	for (std::vector<Timer*>::iterator i = timersCopy.begin();
		i != timersCopy.end(); i++)
	{
		Timer& rTimer = **i;
		ASSERT(!rTimer.HasExpired());
		
		box_time_t timeToExpiry = rTimer.GetExpiryTime() - timeNow;
		
		if (timeToExpiry <= 0)
		{
			rTimer.OnExpire();
		}
	}		
	
	Reschedule();
}

Timer::Timer(size_t timeoutSecs)
: mExpires(GetCurrentBoxTime() + SecondsToBoxTime(timeoutSecs)),
  mExpired(false)
{
	Timers::Add(*this);
}

Timer::~Timer()
{
	Timers::Remove(*this);
}

Timer::Timer(const Timer& rToCopy)
: mExpires(rToCopy.mExpires),
  mExpired(rToCopy.mExpired)
{
	Timers::Add(*this);
}

Timer& Timer::operator=(const Timer& rToCopy)
{
	Timers::Remove(*this);
	mExpires = rToCopy.mExpires;
	mExpired = rToCopy.mExpired;
	if (!mExpired)
	{
		Timers::Add(*this);
	}
}

void Timer::OnExpire()
{
	mExpired = true;
	Timers::Remove(*this);
}
