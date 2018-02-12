// --------------------------------------------------------------------------
//
// File
//		Name:    Logging.cpp
//		Purpose: Generic logging core routines implementation
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <time.h>
#include <string.h> // for stderror

#ifdef HAVE_PROCESS_H
#	include <process.h>
#endif

#ifdef HAVE_SYSLOG_H
#	include <syslog.h>
#endif

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <string>

#include "BoxTime.h"
#include "Exception.h"
#include "Logging.h"

bool Logging::sLogToSyslog  = false;
bool Logging::sLogToConsole = false;
bool Logging::sContextSet   = false;

bool HideExceptionMessageGuard::sHiddenState = false;

std::vector<Logger*> Logging::sLoggers;
std::string Logging::sContext;
Console*    Logging::spConsole = NULL;
Syslog*     Logging::spSyslog  = NULL;
Logging     Logging::sGlobalLogging; // automatic initialisation
std::string Logging::sProgramName;
const Log::Category Logging::UNCATEGORISED("Uncategorised");
std::vector<LogLevelOverrideByFileGuard> Logging::sLogLevelOverrideByFileGuards;

HideSpecificExceptionGuard::SuppressedExceptions_t
	HideSpecificExceptionGuard::sSuppressedExceptions;

Logging::Logging()
{
	ASSERT(!spConsole);
	ASSERT(!spSyslog);
	spConsole = new Console();
	spSyslog  = new Syslog();
	sLogToConsole = true;
	sLogToSyslog  = true;
}

Logging::~Logging()
{
	sLogToConsole = false;
	sLogToSyslog  = false;
	delete spConsole;
	delete spSyslog;
	spConsole = NULL;
	spSyslog  = NULL;
}

void Logging::ToSyslog(bool enabled)
{
	if (!sLogToSyslog && enabled)
	{
		Add(spSyslog);
	}
	
	if (sLogToSyslog && !enabled)
	{
		Remove(spSyslog);
	}
	
	sLogToSyslog = enabled;
}

void Logging::ToConsole(bool enabled)
{
	if (!sLogToConsole && enabled)
	{
		Add(spConsole);
	}
	
	if (sLogToConsole && !enabled)
	{
		Remove(spConsole);
	}
	
	sLogToConsole = enabled;
}

void Logging::FilterConsole(Log::Level level)
{
	spConsole->Filter(level);
}

void Logging::FilterSyslog(Log::Level level)
{
	spSyslog->Filter(level);
}

void Logging::Add(Logger* pNewLogger)
{
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		if (*i == pNewLogger)
		{
			return;
		}
	}
	
	sLoggers.insert(sLoggers.begin(), pNewLogger);
}

void Logging::Remove(Logger* pOldLogger)
{
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		if (*i == pOldLogger)
		{
			sLoggers.erase(i);
			return;
		}
	}
}

void Logging::Log(Log::Level level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += message;
	
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		bool result = (*i)->Log(level, file, line, function, category,
			newMessage);
		if (!result)
		{
			return;
		}
	}
}

void Logging::LogToSyslog(Log::Level level, const std::string& rFile, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	if (!sLogToSyslog)
	{
		return;
	}

	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += message;

	spSyslog->Log(level, rFile, line, function, category, newMessage);
}

void Logging::SetContext(std::string context)
{
	sContext = context;
	sContextSet = true;
}

Log::Level Logging::GetNamedLevel(const std::string& rName)
{
	if      (rName == "nothing") { return Log::NOTHING; }
	else if (rName == "fatal")   { return Log::FATAL; }
	else if (rName == "error")   { return Log::ERROR; }
	else if (rName == "warning") { return Log::WARNING; }
	else if (rName == "notice")  { return Log::NOTICE; }
	else if (rName == "info")    { return Log::INFO; }
	else if (rName == "trace")   { return Log::TRACE; }
	else if (rName == "everything") { return Log::EVERYTHING; }
	else
	{
		BOX_ERROR("Unknown verbosity level: " << rName);
		return Log::INVALID;
	}
}

void Logging::ClearContext()
{
	sContextSet = false;
}

void Logging::SetProgramName(const std::string& rProgramName)
{
	sProgramName = rProgramName;

	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		(*i)->SetProgramName(rProgramName);
	}
}

void Logging::SetFacility(int facility)
{
	spSyslog->SetFacility(facility);
}

Logger::Logger() 
: mCurrentLevel(Log::EVERYTHING) 
{
	Logging::Add(this);
}

Logger::Logger(Log::Level Level) 
: mCurrentLevel(Level) 
{
	Logging::Add(this);
}

Logger::~Logger() 
{
	Logging::Remove(this);
}

bool Logger::IsEnabled(Log::Level level)
{
	return (int)mCurrentLevel >= (int)level;
}

bool Console::sShowTime = false;
bool Console::sShowTimeMicros = false;
bool Console::sShowTag = false;
bool Console::sShowPID = false;
std::string Console::sTag;

void Console::SetProgramName(const std::string& rProgramName)
{
	sTag = rProgramName;
}

void Console::SetShowTag(bool enabled)
{
	sShowTag = enabled;
}

void Console::SetShowTime(bool enabled)
{
	sShowTime = enabled;
}

void Console::SetShowTimeMicros(bool enabled)
{
	sShowTimeMicros = enabled;
}

void Console::SetShowPID(bool enabled)
{
	sShowPID = enabled;
}

bool Logging::ShouldLog(Log::Level default_level, Log::Level message_level,
	const std::string& file, int line, const std::string& function, const Log::Category& category,
	const std::string& message)
{
	// Based on http://stackoverflow.com/a/40947954/648162 and
	// http://stackoverflow.com/a/41367919/648162, but since we have to do this at runtime for
	// each logging call, don't do the strrchr unless we have at least one override configured
	// (otherwise it's never used, and a waste of CPU):
	const char* current_file = NULL;
	if(Logging::sLogLevelOverrideByFileGuards.begin() !=
		Logging::sLogLevelOverrideByFileGuards.end())
	{
		current_file = strrchr(file.c_str(), DIRECTORY_SEPARATOR_ASCHAR) + 1;
	}

	Log::Level level_filter = default_level;
	for(std::vector<LogLevelOverrideByFileGuard>::iterator
		i = Logging::sLogLevelOverrideByFileGuards.begin();
		i != Logging::sLogLevelOverrideByFileGuards.end(); i++)	
	{
		if(i->IsOverridden(message_level, current_file, line, function, category, message))
		{
			level_filter = i->GetNewLevel();
		}
	}

	// Should log if the message level is less than the filter level, otherwise not.
	return (message_level <= level_filter);
}

bool Console::Log(Log::Level level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	if(!Logging::ShouldLog(GetLevel(), level, file, line, function, category, message))
	{
		// Skip the rest of this logger, but continue with the others
		return true;
	}
	
	FILE* target = stdout;
	std::ostringstream buf;

	if (sShowTime)
	{
		buf << FormatTime(GetCurrentBoxTime(), false, sShowTimeMicros);
		buf << " ";
	}

	if (sShowTag)
	{
		if (sShowPID)
		{
			buf << "[" << sTag << " " << getpid() << "] ";
		}
		else
		{
			buf << "[" << sTag << "] ";
		}
	}
	else if (sShowPID)
	{
		buf << "[" << getpid() << "] ";
	}

	if (level <= Log::FATAL)
	{
		buf << "FATAL:   ";
	}
	else if (level <= Log::ERROR)
	{
		buf << "ERROR:   ";
	}
	else if (level <= Log::WARNING)
	{
		buf << "WARNING: ";
	}
	else if (level <= Log::NOTICE)
	{
		buf << "NOTICE:  ";
	}
	else if (level <= Log::INFO)
	{
		buf << "INFO:    ";
	}
	else if (level <= Log::TRACE)
	{
		buf << "TRACE:   ";
	}

	buf << message;

	#ifdef WIN32
		std::string output = buf.str();
		if(ConvertUtf8ToConsole(output.c_str(), output) == false)
		{
			fprintf(target, "%s (and failed to convert to console encoding)\n",
				output.c_str());
		}
		else
		{
			fprintf(target, "%s\n", output.c_str());
		}
	#else
		fprintf(target, "%s\n", buf.str().c_str());
	#endif

	fflush(target);
	
	return true;
}

bool Syslog::Log(Log::Level level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	if(!Logging::ShouldLog(GetLevel(), level, file, line, function, category, message))
	{
		// Skip the rest of this logger, but continue with the others
		return true;
	}
	
	int syslogLevel = LOG_ERR;
	
	switch(level)
	{
		case Log::NOTHING:    /* fall through */
		case Log::INVALID:    /* fall through */
		case Log::FATAL:      syslogLevel = LOG_CRIT;    break;
		case Log::ERROR:      syslogLevel = LOG_ERR;     break;
		case Log::WARNING:    syslogLevel = LOG_WARNING; break;
		case Log::NOTICE:     syslogLevel = LOG_NOTICE;  break;
		case Log::INFO:       syslogLevel = LOG_INFO;    break;
		case Log::TRACE:      /* fall through */
		case Log::EVERYTHING: syslogLevel = LOG_DEBUG;   break;
	}

	std::string msg;

	if (level <= Log::FATAL)
	{
		msg = "FATAL: ";
	}
	else if (level <= Log::ERROR)
	{
		msg = "ERROR: ";
	}
	else if (level <= Log::WARNING)
	{
		msg = "WARNING: ";
	}
	else if (level <= Log::NOTICE)
	{
		msg = "NOTICE: ";
	}

	msg += message;

	syslog(syslogLevel, "%s", msg.c_str());
	
	return true;
}

Syslog::Syslog() : mFacility(LOG_LOCAL6)
{
	::openlog("Box Backup", LOG_PID, mFacility);
}

Syslog::~Syslog()
{
	Shutdown();
}

void Syslog::Shutdown()
{
	::closelog();
}

void Syslog::SetProgramName(const std::string& rProgramName)
{
	mName = rProgramName;
	::closelog();
	::openlog(mName.c_str(), LOG_PID, mFacility);
}

void Syslog::SetFacility(int facility)
{
	mFacility = facility;
	::closelog();
	::openlog(mName.c_str(), LOG_PID, mFacility);
}

int Syslog::GetNamedFacility(const std::string& rFacility)
{
	#define CASE_RETURN(x) if (rFacility == #x) { return LOG_ ## x; }
	CASE_RETURN(LOCAL0)
	CASE_RETURN(LOCAL1)
	CASE_RETURN(LOCAL2)
	CASE_RETURN(LOCAL3)
	CASE_RETURN(LOCAL4)
	CASE_RETURN(LOCAL5)
	CASE_RETURN(LOCAL6)
	CASE_RETURN(DAEMON)
	#undef CASE_RETURN

	BOX_ERROR("Unknown log facility '" << rFacility << "', "
		"using default LOCAL6");
	return LOG_LOCAL6;
}

bool FileLogger::Log(Log::Level Level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	if (mLogFile.StreamClosed())
	{
		/* skip this logger to allow logging failure to open
		the log file, without causing an infinite loop */
		return true;
	}

	if (Level > GetLevel())
	{
		return true;
	}
	
	/* avoid infinite loop if this throws an exception */
	Log::Level oldLevel = GetLevel();
	Filter(Log::NOTHING);

	std::ostringstream buf;
	buf << FormatTime(GetCurrentBoxTime(), true, false);
	buf << " ";

	if (Level <= Log::FATAL)
	{
		buf << "[FATAL]   ";
	}
	else if (Level <= Log::ERROR)
	{
		buf << "[ERROR]   ";
	}
	else if (Level <= Log::WARNING)
	{
		buf << "[WARNING] ";
	}
	else if (Level <= Log::NOTICE)
	{
		buf << "[NOTICE]  ";
	}
	else if (Level <= Log::INFO)
	{
		buf << "[INFO]    ";
	}
	else if (Level <= Log::TRACE)
	{
		buf << "[TRACE]   ";
	}

	buf << message << "\n";
	std::string output = buf.str();

	#ifdef WIN32
		ConvertUtf8ToConsole(output.c_str(), output);
	#endif

	mLogFile.Write(output.c_str(), output.length());

	// no infinite loop, reset to saved logging level
	Filter(oldLevel);
	return true;
}

std::string PrintEscapedBinaryData(const std::string& rInput)
{
	std::ostringstream output;

	for (size_t i = 0; i < rInput.length(); i++)
	{
		if (isprint(rInput[i]))
		{
			output << rInput[i];
		}
		else
		{
			output << "\\x" << std::hex << std::setw(2) <<
				std::setfill('0') << (int) rInput[i] <<
				std::dec;
		}
	}

	return output.str();
}

bool HideSpecificExceptionGuard::IsHidden(int type, int subtype)
{
	for (SuppressedExceptions_t::iterator
		i  = sSuppressedExceptions.begin();
		i != sSuppressedExceptions.end(); i++)
	{
		if(i->first == type && i->second == subtype)
		{
			return true;
		}
	}
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Logging::OptionParser::GetOptionString()
//		Purpose: Returns the valid Getopt command-line options
//			 that Logging::OptionParser::ProcessOption will handle.
//		Created: 2014/04/09
//
// --------------------------------------------------------------------------
std::string Logging::OptionParser::GetOptionString()
{
	return "L:NPqQt:TUvVW:";
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Logging::OptionParser::ProcessOption(signed int option)
//		Purpose: Processes the supplied option (equivalent to the
//			 return code from getopt()). Return zero if the
//			 option was handled successfully, or nonzero to
//			 abort the program with that return value.
//		Created: 2007/09/18
//
// --------------------------------------------------------------------------
int Logging::OptionParser::ProcessOption(signed int option)
{
	switch(option)
	{
		case 'L':
		{
			std::string arg_value(optarg);
			std::string::size_type equals_pos = arg_value.find('=');
			if(equals_pos == std::string::npos)
			{
				BOX_FATAL("Option -L argument should be 'filename[/category]=level'");
				return 2;
			}

			std::string key = arg_value.substr(0, equals_pos);
			std::string filename, category;
			std::string::size_type slash_pos = key.find('/');
			if(slash_pos == std::string::npos)
			{
				// If no category is supplied, assume that the entire key is the filename
				filename = key;
			}
			else
			{
				filename = key.substr(0, slash_pos);
				category = key.substr(slash_pos + 1);
			}

			std::string level_name = arg_value.substr(equals_pos + 1);
			Log::Level level = Logging::GetNamedLevel(level_name);
			if (level == Log::INVALID)
			{
				BOX_FATAL("Invalid logging level: " << level_name);
				return 2;
			}

			sLogLevelOverrideByFileGuards.push_back(
				LogLevelOverrideByFileGuard(filename, category, level, false) // !OverrideAllButSelected
			);
		}
		break;

		case 'N':
		{
			mTruncateLogFile = true;
		}
		break;

		case 'P':
		{
			Console::SetShowPID(true);
		}
		break;

		case 'q':
		{
			if(mCurrentLevel == Log::NOTHING)
			{
				BOX_FATAL("Too many '-q': "
					"Cannot reduce logging "
					"level any more");
				return 2;
			}
			mCurrentLevel--;
		}
		break;

		case 'Q':
		{
			mCurrentLevel = Log::NOTHING;
		}
		break;

		case 't':
		{
			Logging::SetProgramName(optarg);
			Console::SetShowTag(true);
		}
		break;

		case 'T':
		{
			Console::SetShowTime(true);
		}
		break;

		case 'U':
		{
			Console::SetShowTime(true);
			Console::SetShowTimeMicros(true);
		}
		break;

		case 'v':
		{
			if(mCurrentLevel == Log::EVERYTHING)
			{
				BOX_FATAL("Too many '-v': "
					"Cannot increase logging "
					"level any more");
				return 2;
			}
			mCurrentLevel++;
		}
		break;

		case 'V':
		{
			mCurrentLevel = Log::EVERYTHING;
		}
		break;

		case 'W':
		{
			mCurrentLevel = Logging::GetNamedLevel(optarg);
			if (mCurrentLevel == Log::INVALID)
			{
				BOX_FATAL("Invalid logging level: " << optarg);
				return 2;
			}
		}
		break;

		case '?':
		{
			BOX_FATAL("Unknown option on command line: " 
				<< "'" << (char)optopt << "'");
			return 2;
		}
		break;

		default:
		{
			BOX_FATAL("Unknown error in getopt: returned "
				<< "'" << option << "'");
			return 1;
		}
	}

	// If we didn't explicitly return an error code above, then the option
	// was fine, so return 0 to continue processing.
	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Logging::OptionParser::GetUsageString()
//		Purpose: Returns a string suitable for displaying as part
//			 of a program's command-line usage help message,
//			 describing the logging options.
//		Created: 2014/04/09
//
// --------------------------------------------------------------------------
std::string Logging::OptionParser::GetUsageString()
{
	// Based on http://stackoverflow.com/a/40947954/648162 and
	// http://stackoverflow.com/a/41367919/648162:
	const char* current_file = strrchr(__FILE__, DIRECTORY_SEPARATOR_ASCHAR) + 1;

	std::ostringstream buf;
	buf <<
	"  -L [<file>][/<category>]=<level>  Override log level for specified file or\n"
	"             category (for example, -L '" << current_file << "/Configuration=trace').\n"
	"             <category> can be one of: {Uncategorised, Backtrace,\n"
	"             Configuration, RaidFileRead, FileSystem/Locking}\n"
	"  -N         Truncate log file at startup and on backup start\n"
	"  -P         Show process ID (PID) in console output\n"
	"  -q         Run more quietly, reduce verbosity level by one, can repeat\n"
	"  -Q         Run at minimum verbosity, log nothing to console and system\n"
	"  -t <tag>   Tag console output with specified marker\n"
	"  -T         Timestamp console output\n"
	"  -U         Timestamp console output with microseconds\n"
	"  -v         Run more verbosely, increase verbosity level by one, can repeat\n"
	"  -V         Run at maximum verbosity, log everything to console and system\n"
	"  -W <level> Set verbosity to error/warning/notice/info/trace/everything\n";
	return buf.str();
}

bool HideCategoryGuard::Log(Log::Level level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	std::list<Log::Category>::iterator i = std::find(mCategories.begin(),
		mCategories.end(), category);
	// Return false if category is in our list, to suppress further
	// logging (thus, return true if it's not in our list, i.e. we
	// found nothing, to allow it).
	return (i == mCategories.end());
}

LogLevelOverrideByFileGuard::~LogLevelOverrideByFileGuard()
{
	if(mInstalled)
	{
		auto this_pos = std::find(Logging::sLogLevelOverrideByFileGuards.begin(),
			Logging::sLogLevelOverrideByFileGuards.end(), *this);
		ASSERT(this_pos != Logging::sLogLevelOverrideByFileGuards.end());
		Logging::sLogLevelOverrideByFileGuards.erase(this_pos);
	}
}

void LogLevelOverrideByFileGuard::Install()
{
	ASSERT(!mInstalled);
	Logging::sLogLevelOverrideByFileGuards.push_back(*this);
	mInstalled = true;
}

bool LogLevelOverrideByFileGuard::IsOverridden(Log::Level level, const std::string& file, int line,
	const std::string& function, const Log::Category& category,
	const std::string& message)
{
	std::list<std::string>::iterator i = std::find(mFileNames.begin(),
		mFileNames.end(), file);

	// Return true if filename is in our list, i.e. its level is overridden, or if there
	// are no filenames in the list (which corresponds to a wildcard match on filename):
	bool override_matches = (i != mFileNames.end()) || mFileNames.empty();

	// If we have a category, then that must match as well:
	if(override_matches && !mCategoryNamePrefix.empty())
	{
		override_matches = StartsWith(mCategoryNamePrefix, category.ToString());
	}

	if(mOverrideAllButSelected)
	{
		override_matches = !override_matches;
	}

	return override_matches;
}

