// --------------------------------------------------------------------------
//
// File
//		Name:    Logging.cpp
//		Purpose: Generic logging core routines implementation
//		Created: 2006/12/16
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#include "Logging.h"

bool Loggers::sLogToSyslog  = false;
bool Loggers::sLogToConsole = false;
bool Loggers::sContextSet   = false;

void Loggers::ToSyslog(bool enabled)
{
	if (!sLogToSyslog && enabled)
	{
		Add(&sSyslog);
	}
	
	if (sLogToSyslog && !enabled)
	{
		Remove(&sSyslog);
	}
	
	sLogToSyslog = enabled;
}

void Loggers::ToConsole(bool enabled)
{
	if (!sLogToConsole && enabled)
	{
		Add(&sConsole);
	}
	
	if (sLogToConsole && !enabled)
	{
		Remove(&sConsole);
	}
	
	sLogToConsole = enabled;
}

void Loggers::FilterConsole(Log::Level level)
{
	sConsole.Filter(level);
}

void Loggers::FilterSyslog(Log::Level level)
{
	sSyslog.Filter(level);
}

void Loggers::Add(Logger* pNewLogger)
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

void Loggers::Remove(Logger* pOldLogger)
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

void Loggers::Log(Log::Level level, const std::string& rFile, 
	const std::string& rLine, const std::string& rMessage)
{
	std::string newMessage;
	
	if (sContextSet)
	{
		newMessage += "[" + sContext + "] ";
	}
	
	newMessage += rMessage;
	
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		bool result = (*i)->Log(level, rFile, rLine, newMessage);
		if (!result)
		{
			return;
		}
	}
}

void Loggers::SetContext(std::string context)
{
	sContext = context;
	sContextSet = true;
}

void Loggers::ClearContext()
{
	sContextSet = false;
}

void Loggers::SetProgramName(const std::string& rProgramName)
{
	for (std::vector<Logger*>::iterator i = sLoggers.begin();
		i != sLoggers.end(); i++)
	{
		(*i)->SetProgramName(rProgramName);
	}
}

bool Console::Log(Log::Level level, const std::string& rFile, 
	const std::string& rLine, std::string& rMessage)
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
	
	fprintf(target, "%s", rMessage.c_str());
	
	return true;
}

bool Syslog::Log(Log::Level level, const std::string& rFile, 
	const std::string& rLine, std::string& rMessage)
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
	::closelog();
	::openlog(rProgramName.c_str(), LOG_PID, LOG_LOCAL6);
}
