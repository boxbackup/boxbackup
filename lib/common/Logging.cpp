// --------------------------------------------------------------------------
//
// File
//		Name:    Logging.cpp
//		Purpose: Generic logging core routines implementation
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <time.h>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#include "Logging.h"

#include <iomanip>

bool Logging::sLogToSyslog  = false;
bool Logging::sLogToConsole = false;
bool Logging::sContextSet   = false;

std::vector<Logger*> Logging::sLoggers;
std::string Logging::sContext;
Console*    Logging::spConsole = NULL;
Syslog*     Logging::spSyslog  = NULL;
Log::Level  Logging::sGlobalLevel = Log::EVERYTHING;
Logging     Logging::sGlobalLogging; //automatic initialisation

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

void Logging::SetContext(std::string context)
{
	sContext = context;
	sContextSet = true;
}

void Logging::ClearContext()
{
	sContextSet = false;
}

void Logging::SetProgramName(const std::string& rProgramName)
{
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

Logger::~Logger() 
{
	Logging::Remove(this);
}

bool Console::sShowTime = false;
bool Console::sShowTag  = false;
std::string Console::sTag;

void Console::SetTag(const std::string& rTag)
{
	sTag = rTag;
	sShowTag = true;
}

void Console::SetShowTime(bool enabled)
{
	sShowTime = enabled;
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

	std::string msg;

	if (sShowTime)
	{
		struct tm time_now, *tm_ptr = &time_now;
		time_t time_t_now = time(NULL);

		if (time_t_now == ((time_t)-1))
		{
			msg += strerror(errno);
			msg += " ";
		}
		#ifdef WIN32
			else if ((tm_ptr = localtime(&time_t_now)) != NULL)
		#else
			else if (localtime_r(&time_t_now, &time_now) != NULL)
		#endif
		{
			std::ostringstream buf;
			buf << std::setfill('0') <<
				std::setw(2) << tm_ptr->tm_hour << ":" << 
				std::setw(2) << tm_ptr->tm_min  << ":" <<
				std::setw(2) << tm_ptr->tm_sec  << " ";
			msg += buf.str();
		}
		else
		{
			msg += strerror(errno);
			msg += " ";
		}
	}

	if (sShowTag)
	{
		msg += "[" + sTag + "] ";
	}
	
	msg += rMessage;

	fprintf(target, "%s\n", msg.c_str());
	
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
		case Log::FATAL:      syslogLevel = LOG_CRIT;    break;
		case Log::ERROR:      syslogLevel = LOG_ERR;     break;
		case Log::WARNING:    syslogLevel = LOG_WARNING; break;
		case Log::NOTICE:     syslogLevel = LOG_NOTICE;  break;
		case Log::INFO:       syslogLevel = LOG_INFO;    break;
		case Log::TRACE:      /* fall through */
		case Log::EVERYTHING: syslogLevel = LOG_DEBUG;   break;
	}
		
	syslog(syslogLevel, "%s", rMessage.c_str());
	
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
