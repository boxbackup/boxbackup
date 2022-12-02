// --------------------------------------------------------------------------
//
// File
//		Name:    BackgroundTask.h
//		Purpose: Declares the BackgroundTask interface.
//		Created: 2014/04/07
//
// --------------------------------------------------------------------------

#ifndef BACKGROUNDTASK__H
#define BACKGROUNDTASK__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackgroundTask
//		Purpose: Provides a RunBackgroundTask() method which allows
//			 background tasks such as polling the command socket
//			 to happen while a file is being uploaded. If it
//			 returns false, the current task should be aborted.
//		Created: 2014/04/07
//
// --------------------------------------------------------------------------
class BackgroundTask
{
	public:
	enum State {
		Unknown = 0,
		Scanning_Dirs,
		Searching_Blocks,
		Uploading_Full,
		Uploading_Patch
	};
	virtual ~BackgroundTask() { }
	virtual bool RunBackgroundTask(State state, uint64_t progress,
		uint64_t maximum) = 0;
};

#endif // BACKGROUNDTASK__H
