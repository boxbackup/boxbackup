// --------------------------------------------------------------------------
//
// File
//		Name:    Timer.h
//		Purpose: Generic timers which execute arbitrary code when
//			 they expire.
//		Created: 5/11/2006
//
// --------------------------------------------------------------------------

#ifndef TIMER__H
#define TIMER__H

#ifdef HAVE_SYS_TIME_H
	#include <sys/time.h>
#endif

#include <vector>

#include "BoxTime.h"

#include "MemLeakFindOn.h"

class Timer;

// --------------------------------------------------------------------------
//
// Class
//		Name:    Timers
//		Purpose: Static class to manage all timers and arrange 
//			 efficient delivery of wakeup signals
//		Created: 19/3/04
//
// --------------------------------------------------------------------------
class Timers
{
	private:
	static std::vector<Timer*>* spTimers;
	static void Reschedule();

	static bool sRescheduleNeeded;
	static void SignalHandler(int iUnused);

	public:
	static void Init();
	static void Cleanup(bool throw_exception_if_not_initialised = true);
	static void Add   (Timer& rTimer);
	static void Remove(Timer& rTimer);
	static void RequestReschedule();
	static void RescheduleIfNeeded();
};

class Timer
{
public:
	Timer(size_t timeoutMillis, const std::string& rName = "");
	virtual ~Timer();
	Timer(const Timer &);
	Timer &operator=(const Timer &);

	box_time_t   GetExpiryTime() { return mExpires; }
	virtual void OnExpire();
	bool         HasExpired()
	{
		Timers::RescheduleIfNeeded();
		return mExpired; 
	}

	const std::string& GetName() const { return mName; }
	virtual void Reset(size_t timeoutMillis);
	
private:
	box_time_t  mExpires;
	bool        mExpired;
	std::string mName;

	void Start();
	void Start(int64_t timeoutMillis);
	void Stop();
	void LogAssignment(const Timer &From);
	virtual void Set(size_t timeoutMillis, bool isReset);

	#ifdef WIN32
	HANDLE mTimerHandle;
	static VOID CALLBACK TimerRoutine(PVOID lpParam,
		BOOLEAN TimerOrWaitFired);
	#endif
};

#include "MemLeakFindOff.h"

#endif // TIMER__H
