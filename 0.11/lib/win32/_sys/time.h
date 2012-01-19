#ifndef EMU_SYS_TIME_H
#	define EMU_SYS_TIME_H

#	ifdef WIN32
#		pragma once

#		define EMU_UTIMES emu_utimes

		extern int emu_utimes(const char * pName, const struct timeval times[]) throw();
#	else
#		define EMU_UTIMES ::utimes
#	endif

#endif
