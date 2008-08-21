// --------------------------------------------------------------------------
//
// File
//		Name:    RunStatusProvider.h
//		Purpose: Declares the RunStatusProvider interface.
//		Created: 2008/08/14
//
// --------------------------------------------------------------------------

#ifndef RUNSTATUSPROVIDER__H
#define RUNSTATUSPROVIDER__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    RunStatusProvider
//		Purpose: Provides a StopRun() method which returns true if
//			 the current backup should be halted.
//		Created: 2005/11/15
//
// --------------------------------------------------------------------------
class RunStatusProvider
{
	public:
	virtual ~RunStatusProvider() { }
	virtual bool StopRun() = 0;
};

#endif // RUNSTATUSPROVIDER__H
