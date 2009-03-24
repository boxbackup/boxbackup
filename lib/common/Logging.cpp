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

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <cstring>
#include <iomanip>

#include "BoxTime.h"
#include "Logging.h"

bool Logging::sLogToSyslog  = false;
bool Logging::sLogToConsole = false;
bool Logging::sContextSet   = false;

std::vector<Logger*> Logging::sLoggers;
std::string Logging::sContext;
Console*    Logging::spConsole = NULL;
Syslog*     Logging::spSyslog  = NULL;
Log::Level  Logging::sGlobalLevel = Log::EVERYTHING;
Logging     Logging::sGlobalLogging; //automatic initialisation
std::string Logging::sProgramName;

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

void Logging::Log(Log::Level level, const std::string& rFile, 
	int line, const std::string& rMessage)
{
	if (level > sGlobalLevel)
	{
		return;
	}

	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += rMessage;
	
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		bool result = (*i)->Log(level, rFile, line, newMessage);
		if (!result)
		{
			return;
		}
	}
}

void Logging::LogToSyslog(Log::Level level, const std::string& rFile, 
	int line, const std::string& rMessage)
{
	if (!sLogToSyslog)
	{
		return;
	}

	if (level > sGlobalLevel)
	{
		return;
	}

	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += rMessage;

	spSyslog->Log(level, rFile, line, newMessage);
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

bool Console::Log(Log::Level level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (level > GetLevel())
	{
		return true;
	}
	
	FILE* target = stdout;
	
	if (level <= Log::WARNING)
	{
		target = stderr;
	}

	std::ostringstream buf;

	if (sShowTime)
	{
		buf << FormatTime(GetCurrentBoxTime(), sShowTimeMicros);
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

	buf << rMessage;

	#ifdef WIN32
		std::string output = buf.str();
		ConvertUtf8ToConsole(output.c_str(), output);
		fprintf(target, "%s\n", output.c_str());
	#else
		fprintf(target, "%s\n", buf.str().c_str());
	#endif
	
	return true;
}

bool Syslog::Log(Log::Level level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (level > GetLevel())
	{
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

	msg += rMessage;

	syslog(syslogLevel, "%s", msg.c_str());
	
	return true;
}

Syslog::Syslog()
{
	::openlog("Box Backup", LOG_PID, LOG_LOCAL6);
}

Syslog::~Syslog()
{
	::closelog();
}

void Syslog::SetProgramName(const std::string& rProgramName)
{
	mName = rProgramName;
	::closelog();
	::openlog(mName.c_str(), LOG_PID, LOG_LOCAL6);
}

bool FileLogger::Log(Log::Level Level, const std::string& rFile, 
	int line, std::string& rMessage)
{
	if (Level > GetLevel())
	{
		return true;
	}
	
	/* avoid infinite loop if this throws an exception */
	Logging::Remove(this);

	std::ostringstream buf;
	buf << FormatTime(GetCurrentBoxTime(), false);
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

	buf << rMessage << "\n";
	std::string output = buf.str();

	#ifdef WIN32
		ConvertUtf8ToConsole(output.c_str(), output);
	#endif

	mLogFile.Write(output.c_str(), output.length());

	Logging::Add(this);
	return true;
}
