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

#include <assert.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <list>
#include <sstream>
#include <vector>

#include "FileStream.h"

#define BOX_LOG(level, stuff) \
{ \
	std::ostringstream _box_log_line; \
	_box_log_line << stuff; \
	Logging::Log(level, BOX_CURRENT_FILE, __LINE__, __FUNCTION__, \
		Logging::UNCATEGORISED, _box_log_line.str()); \
}

#define BOX_LOG_CATEGORY(level, category, stuff) \
{ \
	std::ostringstream _box_log_line; \
	_box_log_line << stuff; \
	Logging::Log(level, __FILE__, __LINE__, __FUNCTION__, \
		category, _box_log_line.str()); \
}

#define BOX_SYSLOG(level, stuff) \
{ \
	std::ostringstream _box_log_line; \
	_box_log_line << stuff; \
	Logging::LogToSyslog(level, __FILE__, __LINE__, __FUNCTION__, \
		Logging::UNCATEGORISED, _box_log_line.str()); \
}

#define BOX_FATAL(stuff)   BOX_LOG(Log::FATAL,   stuff)
#define BOX_ERROR(stuff)   BOX_LOG(Log::ERROR,   stuff)
#define BOX_WARNING(stuff) BOX_LOG(Log::WARNING, stuff)
#define BOX_NOTICE(stuff)  BOX_LOG(Log::NOTICE,  stuff)
#define BOX_INFO(stuff)    BOX_LOG(Log::INFO,    stuff)
#define BOX_TRACE(stuff)   BOX_LOG(Log::TRACE,   stuff)

#define BOX_SYS_ERRNO_MESSAGE(error_number, stuff) \
	stuff << ": " << std::strerror(error_number) << \
	" (" << error_number << ")"

#define BOX_FILE_MESSAGE(filename, message) \
	message << ": " << filename

#define BOX_SYS_FILE_ERRNO_MESSAGE(filename, error_number, message) \
	BOX_SYS_ERRNO_MESSAGE(error_number, BOX_FILE_MESSAGE(filename, message))

#define BOX_SYS_ERROR_MESSAGE(stuff) \
	BOX_SYS_ERRNO_MESSAGE(errno, stuff)

#define BOX_LOG_SYS_WARNING(stuff) \
	BOX_WARNING(BOX_SYS_ERROR_MESSAGE(stuff))
#define BOX_LOG_SYS_ERROR(stuff) \
	BOX_ERROR(BOX_SYS_ERROR_MESSAGE(stuff))
#define BOX_LOG_SYS_ERRNO(error_number, stuff) \
	BOX_ERROR(BOX_SYS_ERRNO_MESSAGE(error_number, stuff))
#define BOX_LOG_SYS_FATAL(stuff) \
	BOX_FATAL(BOX_SYS_ERROR_MESSAGE(stuff))

#define THROW_SYS_ERROR_NUMBER(message, error_number, exception, subtype) \
	THROW_EXCEPTION_MESSAGE(exception, subtype, \
		BOX_SYS_ERRNO_MESSAGE(error_number, message))

#define THROW_SYS_ERROR(message, exception, subtype) \
	THROW_SYS_ERROR_NUMBER(message, errno, exception, subtype)

#define THROW_SYS_FILE_ERROR(message, filename, exception, subtype) \
	THROW_SYS_ERROR_NUMBER(BOX_FILE_MESSAGE(filename, message), \
		errno, exception, subtype)

#define THROW_SYS_FILE_ERRNO(message, filename, error_number, exception, subtype) \
	THROW_SYS_ERROR_NUMBER(BOX_FILE_MESSAGE(filename, message), \
		error_number, exception, subtype)

#define THROW_FILE_ERROR(message, filename, exception, subtype) \
	THROW_EXCEPTION_MESSAGE(exception, subtype, \
		BOX_FILE_MESSAGE(filename, message))

#ifdef WIN32
	#define BOX_WIN_ERRNO_MESSAGE(error_number, stuff) \
		stuff << ": " << GetErrorMessage(error_number)
	#define BOX_NATIVE_ERRNO_MESSAGE(error_number, stuff) \
		BOX_WIN_ERRNO_MESSAGE(error_number, stuff)
	#define BOX_LOG_WIN_ERROR(stuff) \
		BOX_ERROR(BOX_WIN_ERRNO_MESSAGE(GetLastError(), stuff))
	#define BOX_LOG_WIN_WARNING(stuff) \
		BOX_WARNING(BOX_WIN_ERRNO_MESSAGE(GetLastError(), stuff))
	#define BOX_LOG_WIN_ERROR_NUMBER(stuff, number) \
		BOX_ERROR(BOX_WIN_ERRNO_MESSAGE(number, stuff))
	#define BOX_LOG_WIN_WARNING_NUMBER(stuff, number) \
		BOX_WARNING(BOX_WIN_ERRNO_MESSAGE(number, stuff))
	#define BOX_LOG_NATIVE_ERROR(stuff)   BOX_LOG_WIN_ERROR(stuff)
	#define BOX_LOG_NATIVE_WARNING(stuff) BOX_LOG_WIN_WARNING(stuff)
	#define THROW_WIN_ERROR_NUMBER(message, error_number, exception, subtype) \
		THROW_EXCEPTION_MESSAGE(exception, subtype, \
			BOX_WIN_ERRNO_MESSAGE(error_number, message))
	#define THROW_WIN_FILE_ERRNO(message, filename, error_number, exception, subtype) \
		THROW_WIN_ERROR_NUMBER(BOX_FILE_MESSAGE(filename, message), \
			error_number, exception, subtype)
	#define THROW_WIN_FILE_ERROR(message, filename, exception, subtype) \
		THROW_WIN_FILE_ERRNO(message, filename, GetLastError(), \
			exception, subtype)
	#define EMU_ERRNO winerrno
	#define THROW_EMU_ERROR(message, exception, subtype) \
		THROW_EXCEPTION_MESSAGE(exception, subtype, \
			BOX_NATIVE_ERRNO_MESSAGE(EMU_ERRNO, message))
#else
	#define BOX_NATIVE_ERRNO_MESSAGE(error_number, stuff) \
		BOX_SYS_ERRNO_MESSAGE(error_number, stuff)
	#define BOX_LOG_NATIVE_ERROR(stuff)   BOX_LOG_SYS_ERROR(stuff)
	#define BOX_LOG_NATIVE_WARNING(stuff) BOX_LOG_SYS_WARNING(stuff)
	#define EMU_ERRNO errno
	#define THROW_EMU_ERROR(message, exception, subtype) \
		THROW_EXCEPTION_MESSAGE(exception, subtype, \
			BOX_SYS_ERRNO_MESSAGE(EMU_ERRNO, message))
#endif

#define THROW_EMU_FILE_ERROR(message, filename, exception, subtype) \
	THROW_EMU_ERROR(BOX_FILE_MESSAGE(filename, message), \
		exception, subtype)

#ifdef WIN32
#	define BOX_SOCKET_ERROR_MESSAGE(_type, _name, _port, stuff) \
	BOX_WIN_ERRNO_MESSAGE(WSAGetLastError(), stuff << " (type " << _type << \
		", name " << _name << ", port " << _port << ")")
#else
#	define BOX_SOCKET_ERROR_MESSAGE(_type, _name, _port, stuff) \
	BOX_SYS_ERROR_MESSAGE(stuff << " (type " << _type << ", name " << _name << \
		", port " << _port << ")")
#endif

#define BOX_FORMAT_HEX32(number) \
	std::hex << \
	std::showbase << \
	std::internal << \
	std::setw(10) << \
	std::setfill('0') << \
	(number) << \
	std::dec

#define BOX_FORMAT_ACCOUNT(accno) \
	BOX_FORMAT_HEX32(accno)

#define BOX_FORMAT_OBJECTID(objectid) \
	std::hex << \
	std::showbase << \
	(objectid) << \
	std::dec

#define BOX_FORMAT_TIMESPEC(timespec) \
	timespec.tv_sec << \
	"." << \
	std::setw(6) << \
	std::setfill('0') << \
	timespec.tv_usec

#define BOX_FORMAT_MICROSECONDS(t) \
	(int)((t) / 1000000) << "." << \
	std::setw(3) << \
	std::setfill('0') << \
	(int)((t % 1000000) / 1000) << " seconds"

#undef ERROR

namespace Log
{
	enum Level 
	{
		NOTHING = 1,
		FATAL,
		ERROR,
		WARNING,
		NOTICE,
		INFO,
		TRACE, 
		EVERYTHING,
		INVALID = -1
	};

	class Category {
	private:
		std::string mName;

	public:
		Category(const std::string& name)
		: mName(name)
		{ }
		const std::string& ToString() { return mName; }
		bool operator==(const Category& other) { return mName == other.mName; }
	};
}

// --------------------------------------------------------------------------
//
// Class
//		Name:    Logger
//		Purpose: Abstract base class for log targets
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

class Logger
{
	private:
	Log::Level mCurrentLevel;
	
	public:
	Logger();
	Logger(Log::Level level);
	virtual ~Logger();
	
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message) = 0;
	
	void Filter(Log::Level level)
	{
		mCurrentLevel = level;
	}

	virtual const char* GetType() = 0;
	Log::Level GetLevel() { return mCurrentLevel; }
	bool IsEnabled(Log::Level level);

	virtual void SetProgramName(const std::string& rProgramName) = 0;

	class LevelGuard
	{
		private:
		Logger& mLogger;
		Log::Level mOldLevel;

		public:
		LevelGuard(Logger& Logger, Log::Level newLevel = Log::INVALID)
		: mLogger(Logger)
		{
			mOldLevel = Logger.GetLevel();
			if (newLevel != Log::INVALID)
			{
				Logger.Filter(newLevel);
			}
		}
		~LevelGuard()
		{
			mLogger.Filter(mOldLevel);
		}
	};
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Console
//		Purpose: Console logging target
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

class Console : public Logger
{
	private:
	static bool sShowTag;
	static bool sShowTime;
	static bool sShowTimeMicros;
	static bool sShowPID;
	static std::string sTag;

	public:
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	virtual const char* GetType() { return "Console"; }
	virtual void SetProgramName(const std::string& rProgramName);

	static void SetShowTag(bool enabled);
	static void SetShowTime(bool enabled);
	static void SetShowTimeMicros(bool enabled);
	static void SetShowPID(bool enabled);
	static bool GetShowTag() { return sShowTag; }

	class SettingsGuard
	{
		private:
		bool mShowTag;
		bool mShowTime;
		bool mShowTimeMicros;
		bool mShowPID;
		std::string mTag;
		public:
		SettingsGuard()
		: mShowTag(Console::sShowTag),
		  mShowTime(Console::sShowTime),
		  mShowTimeMicros(Console::sShowTimeMicros),
		  mShowPID(Console::sShowPID),
		  mTag(Console::sTag)
		{ }
		~SettingsGuard()
		{
			Console::SetShowTag(mShowTag);
			Console::SetShowTime(mShowTime);
			Console::SetShowTimeMicros(mShowTimeMicros);
			Console::SetShowPID(mShowPID);
			Console::sTag = mTag;
		}
	};
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Syslog
//		Purpose: Syslog (or Windows Event Viewer) logging target
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

class Syslog : public Logger
{
	private:
	std::string mName;
	int mFacility;

	public:
	Syslog();
	virtual ~Syslog();
	
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	virtual const char* GetType() { return "Syslog"; }
	virtual void SetProgramName(const std::string& rProgramName);
	virtual void SetFacility(int facility);
	virtual void Shutdown();
	static int GetNamedFacility(const std::string& rFacility);
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    Capture
//		Purpose: Keeps log messages for analysis in tests.
//		Created: 2014/03/08
//
// --------------------------------------------------------------------------

class Capture : public Logger
{
	public:
	struct Message
	{
		Message(const Log::Category& category)
		: mCategory(category) { }
		Log::Level level;
		std::string file;
		int line;
		std::string function;
		Log::Category mCategory;
		std::string message;
	};

	private:
	std::vector<Message> mMessages;
	
	public:
	virtual ~Capture() { }
	
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message)
	{
		Message msg(category);
		msg.level = level;
		msg.file = file;
		msg.line = line;
		msg.function = function;
		msg.message = message;
		mMessages.push_back(msg);
		return true;
	}
	virtual const char* GetType() { return "Capture"; }
	virtual void SetProgramName(const std::string& rProgramName) { }
	const std::vector<Message>& GetMessages() const { return mMessages; }
	std::string GetString() const
	{
		std::ostringstream oss;
		for (std::vector<Message>::const_iterator i = mMessages.begin();
			i != mMessages.end(); i++)
		{
			oss << i->message << "\n";
		}
		return oss.str();
	}
};

class LogLevelOverrideByFileGuard
{
	private:
	std::list<std::string> mFileNames;
	Log::Level mNewLevel;
	bool mOverrideAllButSelected;

	public:
	LogLevelOverrideByFileGuard(const std::string& rFileName, Log::Level NewLevel,
		bool OverrideAllButSelected = false)
	: mNewLevel(NewLevel), mOverrideAllButSelected(OverrideAllButSelected)
	{
		mFileNames.push_back(rFileName);
	}
	virtual ~LogLevelOverrideByFileGuard()
	{
	}
	void Add(const std::string& rFileName)
	{
		mFileNames.push_back(rFileName);
	}
	bool IsOverridden(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	Log::Level GetNewLevel() { return mNewLevel; }
};


// --------------------------------------------------------------------------
//
// Class
//		Name:    Logging
//		Purpose: Static logging helper, keeps track of enabled loggers
//			 and distributes log messages to them.
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

class Logging
{
	private:
	static std::vector<Logger*> sLoggers;
	static bool sLogToSyslog, sLogToConsole;
	static std::string sContext;
	static bool sContextSet;
	static Console* spConsole;
	static Syslog*  spSyslog;
	static Logging    sGlobalLogging;
	static std::string sProgramName;
	static std::vector<LogLevelOverrideByFileGuard> sLogLevelOverrideByFileGuards;

	public:
	Logging ();
	~Logging();
	static void ToSyslog  (bool enabled);
	static void ToConsole (bool enabled);
	static void FilterSyslog  (Log::Level level);
	static void FilterConsole (Log::Level level);
	static void Add    (Logger* pNewLogger);
	static void Remove (Logger* pOldLogger);
	static bool ShouldLog(Log::Level default_level, Log::Level message_level,
		const std::string& file, int line, const std::string& function,
		const Log::Category& category, const std::string& message);
	static void Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	static void LogToSyslog(Log::Level level, const std::string& rFile, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	static void SetContext(std::string context);
	static void ClearContext();
	static Log::Level GetNamedLevel(const std::string& rName);
	static void SetProgramName(const std::string& rProgramName);
	static std::string GetProgramName() { return sProgramName; }
	static void SetFacility(int facility);
	static Console& GetConsole() { return *spConsole; }
	static Syslog&  GetSyslog()  { return *spSyslog; }

	class ShowTagOnConsole
	{
		private:
		bool mOldShowTag;
		
		public:
		ShowTagOnConsole()
		: mOldShowTag(Console::GetShowTag())
		{
			Console::SetShowTag(true);
		}
		~ShowTagOnConsole()
		{
			Console::SetShowTag(mOldShowTag);
		}
	};

	class Tagger
	{
		private:
		std::string mOldTag;
		bool mReplace;

		public:
		Tagger()
		: mOldTag(Logging::GetProgramName()),
		  mReplace(false)
		{
		}
		Tagger(const std::string& rTempTag, bool replace = false)
		: mOldTag(Logging::GetProgramName()),
		  mReplace(replace)
		{
			Change(rTempTag);
		}
		~Tagger()
		{
			Logging::SetProgramName(mOldTag);
		}

		void Change(const std::string& newTempTag)
		{
			if(mReplace || mOldTag.empty())
			{
				Logging::SetProgramName(newTempTag);
			}
			else
			{
				Logging::SetProgramName(mOldTag + " " + newTempTag);
			}
		}
	};

	class TempLoggerGuard
	{
		private:
		Logger* mpLogger;

		public:
		TempLoggerGuard(Logger* pLogger)
		: mpLogger(pLogger)
		{
			Logging::Add(mpLogger);
		}
		~TempLoggerGuard()
		{
			Logging::Remove(mpLogger);
		}
	};

	// Process global options
	static std::string GetOptionString();
	static int ProcessOption(signed int option);
	static std::string GetUsageString();

	// --------------------------------------------------------------------------
	//
	// Class
	//		Name:    Logging::OptionParser
	//		Purpose: Process command-line options, some global, some local
	//		Created: 2014/04/09
	//
	// --------------------------------------------------------------------------
	class OptionParser
	{
	public:
		OptionParser(Log::Level InitialLevel =
			#ifdef BOX_RELEASE_BUILD
			Log::NOTICE
			#else
			Log::INFO
			#endif
			)
		: mCurrentLevel(InitialLevel),
		  mTruncateLogFile(false)
		{ }
		
		static std::string GetOptionString();
		int ProcessOption(signed int option);
		static std::string GetUsageString();
		int mCurrentLevel; // need an int to do math with
		bool mTruncateLogFile;
		Log::Level GetCurrentLevel()
		{
			return (Log::Level) mCurrentLevel;
		}
	};

	static const Log::Category UNCATEGORISED;
};

class FileLogger : public Logger
{
	private:
	FileStream mLogFile;
	FileLogger(const FileLogger& forbidden)
	: mLogFile("") { /* do not call */ }
	
	public:
	FileLogger(const std::string& rFileName, Log::Level Level, bool append)
	: Logger(Level),
	  mLogFile(rFileName, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC))
	{ }
	
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	
	virtual const char* GetType() { return "FileLogger"; }
	virtual void SetProgramName(const std::string& rProgramName) { }
};

class HideExceptionMessageGuard
{
	public:
	HideExceptionMessageGuard()
	{
		mOldHiddenState = sHiddenState;
		sHiddenState = true;
	}
	~HideExceptionMessageGuard()
	{
		sHiddenState = mOldHiddenState;
	}
	static bool ExceptionsHidden() { return sHiddenState; }

	private:
	static bool sHiddenState;
	bool mOldHiddenState;
};

class HideSpecificExceptionGuard
{
	private:
	std::pair<int, int> mExceptionCode;

	public:
	typedef std::vector<std::pair<int, int> > SuppressedExceptions_t;
	static SuppressedExceptions_t sSuppressedExceptions;

	HideSpecificExceptionGuard(int type, int subtype)
	: mExceptionCode(std::pair<int, int>(type, subtype))
	{
		sSuppressedExceptions.push_back(mExceptionCode);
	}
	~HideSpecificExceptionGuard()
	{
		SuppressedExceptions_t::reverse_iterator i =
			sSuppressedExceptions.rbegin();
		assert(*i == mExceptionCode);
		sSuppressedExceptions.pop_back();
	}
	static bool IsHidden(int type, int subtype);
};

class HideCategoryGuard : public Logger
{
	private:
	std::list<Log::Category> mCategories;
	HideCategoryGuard(const HideCategoryGuard& other); // no copying
	HideCategoryGuard& operator=(const HideCategoryGuard& other); // no assignment

	public:
	HideCategoryGuard(const Log::Category& category)
	{
		mCategories.push_back(category);
		Logging::Add(this);
	}
	~HideCategoryGuard()
	{
		Logging::Remove(this);
	}
	void Add(const Log::Category& category)
	{
		mCategories.push_back(category);
	}
	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message);
	virtual const char* GetType() { return "HideCategoryGuard"; }
	virtual void SetProgramName(const std::string& rProgramName) { }
};

std::string PrintEscapedBinaryData(const std::string& rInput);

#endif // LOGGING__H
