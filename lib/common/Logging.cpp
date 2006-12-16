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

bool Logging::sLogToSyslog  = false;
bool Logging::sLogToConsole = false;
bool Logging::sContextSet   = false;

std::vector<Logger*> Logging::sLoggers;
std::string Logging::sContext;
Console    Logging::sConsole;
Syslog     Logging::sSyslog;
Log::Level  Logging::sGlobalLevel;

void Logging::ToSyslog(bool enabled)
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

void Logging::ToConsole(bool enabled)
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

void Logging::FilterConsole(Log::Level level)
{
	sConsole.Filter(level);
}

void Logging::FilterSyslog(Log::Level level)
{
	sSyslog.Filter(level);
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
	
	fprintf(target, "%s\n", rMessage.c_str());
	
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
