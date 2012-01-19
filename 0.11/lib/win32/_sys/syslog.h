#ifndef EMU_SYS_SYSLOG_H
#	define EMU_SYS_SYSLOG_H

#	ifdef WIN32
#		pragma once

#		define LOG_DEBUG LOG_INFO
#		define LOG_INFO 6
#		define LOG_NOTICE LOG_INFO
#		define LOG_WARNING 4
#		define LOG_ERR 3
#		define LOG_CRIT LOG_ERR
#		define LOG_PID 0
#		define LOG_LOCAL0 0
#		define LOG_LOCAL1 0
#		define LOG_LOCAL2 0
#		define LOG_LOCAL3 0
#		define LOG_LOCAL4 0
#		define LOG_LOCAL5 0
#		define LOG_LOCAL6 0
#		define LOG_DAEMON 0

		extern void openlog (const char * daemonName, int, int) throw();
		extern void closelog(void) throw();
		extern void syslog  (int loglevel, const char *fmt, ...) throw();
#	endif

#endif
