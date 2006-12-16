// --------------------------------------------------------------------------
//
// File
//		Name:    Logging.h
//		Purpose: Generic logging core routines declarations and macros
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

#ifndef LOGGING__H
#define LOGGING__H

#include <sstream>
#include <vector>

#define BOX_LOG(level, stuff) \
{ \
    if(Log::sMaxLoggingLevelForAnyOutput >= level) \
        std::ostringstream line; \
        line << stuff; \
        Log::Write(level, __FILE__, __LINE__, line.str()); \
    } \
}

#define BOX_FATAL(stuff)   BOX_LOG(Log::FATAL,   stuff)
#define BOX_ERROR(stuff)   BOX_LOG(Log::ERROR,   stuff)
#define BOX_WARNING(stuff) BOX_LOG(Log::WARNING, stuff)
#define BOX_INFO(stuff)    BOX_LOG(Log::INFO,    stuff)
#if defined NDEBUG && ! defined COMPILE_IN_TRACES
        #define BOX_TRACE(stuff)   
#else
        #define BOX_TRACE(stuff)   BOX_LOG(Log::TRACE, stuff)
#endif

namespace Log
{
	enum Level { NOTHING = 1, FATAL, ERROR, WARNING, INFO, TRACE, EVERYTHING };
}

// --------------------------------------------------------------------------
//
// Class
//		Name:    Logger
//		Purpose: Abstract class (interface) for log targets
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

class Logger
{
	private:
	Log::Level mCurrentLevel;
	
	public:
	Logger() : mCurrentLevel(Log::WARNING) { }
	virtual ~Logger() { }
	
	virtual bool Log(Log::Level level, const std::string& rFile, 
		const std::string& rLine, std::string& rMessage) = 0;
	
	void Filter(Log::Level level)
	{
		mCurrentLevel = level;
	}

	virtual const char* GetType() = 0;
	Log::Level GetLevel() { return mCurrentLevel; }
	
	virtual void SetProgramName(const std::string& rProgramName) = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Console
//		Purpose: Console logging target
//		Created: 2006/12/12
//
// --------------------------------------------------------------------------

class Console : public Logger
{
	public:
	virtual bool Log(Log::Level level, const std::string& rFile, 
		const std::string& rLine, std::string& rMessage);
	virtual const char* GetType() { return "Console"; }
	virtual void SetProgramName(const std::string& rProgramName) { }
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Syslog
//		Purpose: Syslog (or Windows Event Viewer) logging target
//		Created: 2006/12/12
//
// --------------------------------------------------------------------------

class Syslog : public Logger
{
	public:
	Syslog();
	virtual ~Syslog();
	
	virtual bool Log(Log::Level level, const std::string& rFile, 
		const std::string& rLine, std::string& rMessage);
	virtual const char* GetType() { return "Syslog"; }
	virtual void SetProgramName(const std::string& rProgramName);
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Log
//		Purpose: Static logging helper, keeps track of enabled loggers
//			 and distributes log messages to them.
//		Created: 2006/12/12
//
// --------------------------------------------------------------------------

class Loggers
{
	private:
	static std::vector<Logger*> sLoggers;
	static bool sLogToSyslog, sLogToConsole;
	static std::string sContext;
	static bool sContextSet;
	static Console sConsole;
	static Syslog  sSyslog;
	static Log::Level sGlobalLevel;
	
	public:
	static void Init(const std::string& rProgramName);
	static void ToSyslog  (bool enabled);
	static void ToConsole (bool enabled);
	static void FilterSyslog  (Log::Level level);
	static void FilterConsole (Log::Level level);
	static void Add    (Logger* pNewLogger);
	static void Remove (Logger* pOldLogger);
	static void Log(Log::Level level, const std::string& rFile, 
		const std::string& rLine, const std::string& rMessage);
	static void SetContext(std::string context);
	static void ClearContext();
	static void SetGlobalLevel(Log::Level level) { sGlobalLevel = level; }
	static void SetProgramName(const std::string& rProgramName);
};

#endif // LOGGING__H
