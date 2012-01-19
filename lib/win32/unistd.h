#ifndef EMU_UNISTD_H
#	define EMU_UNISTD_H

#	ifdef WIN32
#		pragma once
#		ifndef _SSIZE_T_DEFINED
#			ifdef  _WIN64
				typedef __int64    ssize_t;
#			else
				typedef __int32    ssize_t;
#			endif
#			define _SSIZE_T_DEFINED
#		endif


		typedef unsigned int mode_t;
		typedef unsigned int pid_t;

		extern int chown(const char * Filename, uint32_t uid, uint32_t gid) throw();

		extern int setegid(int) throw();
		extern int seteuid(int) throw();
		extern int setgid(int) throw();
		extern int setuid(int) throw();
		extern int getgid(void) throw();
		extern int getuid(void) throw();
		extern int geteuid(void) throw();

		extern unsigned int sleep(unsigned int secs) throw();

#	endif

#endif
