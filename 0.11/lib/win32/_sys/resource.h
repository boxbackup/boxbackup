#ifndef EMU_SYS_RESOURCE_H
#	define EMU_SYS_RESOURCE_H

#	ifdef WIN32
#		pragma once

		extern int waitpid(pid_t pid, int *status, int) throw();
#	endif

#endif
